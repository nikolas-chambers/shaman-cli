#include "shaman/server/server.hpp"

#include <chrono>
#include <iostream>
#include <list>
#include <regex>
#include <thread>

#include "shaman/command/command.hpp"
#include "shaman/core/id.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/net.hpp"
#include "shaman/version.hpp"

namespace shaman::embedded {
extern const std::string_view web_index;
}

namespace shaman::server {
namespace {

using namespace std::chrono_literals;

// Pending permission questions, answered via POST /permission/:id.
struct Pending {
  std::mutex mu;
  std::condition_variable cv;
  std::map<std::string, std::optional<permission::Reply>> replies;
};

// Global event bus for GET /event subscribers.
struct Bus {
  std::mutex mu;
  std::list<net::Connection*> subscribers;
  void publish(const std::string& type, const Json& data) {
    std::lock_guard lock(mu);
    auto payload = data.dump();
    for (auto it = subscribers.begin(); it != subscribers.end();)
      it = (*it)->sse(type, payload) ? std::next(it) : subscribers.erase(it);
  }
};

// Per-session runtime: one Runner (keeps read files, todos, "always" grants) and a cancel flag.
struct Live {
  std::mutex busy;
  std::atomic<bool> cancel{false};
  std::unique_ptr<session::Runner> runner;
};

class StreamEvents final : public session::Events {
 public:
  StreamEvents(net::Connection& c, Bus& bus, std::string sid) : c_(c), bus_(bus), sid_(std::move(sid)) {}
  void emit(const std::string& type, Json data) {
    data["session"] = sid_;
    if (open_) open_ = c_.sse(type, data.dump());
    bus_.publish(type, data);
  }
  void text(std::string_view t) override { emit("text", {{"text", t}}); }
  void reasoning(std::string_view t) override { emit("reasoning", {{"text", t}}); }
  void tool_start(const llm::ToolCallPart& c) override { emit("tool_start", {{"id", c.id}, {"name", c.name}, {"input", c.input}}); }
  void tool_end(const llm::ToolCallPart& c, const tool::Output& o) override {
    emit("tool_end", {{"id", c.id}, {"name", c.name}, {"title", o.title}, {"is_error", o.is_error}, {"output", o.text}});
  }
  void step_end(int step, const llm::Usage& u, const std::string& model) override {
    emit("step", {{"step", step}, {"model", model}, {"input_tokens", u.input}, {"output_tokens", u.output}});
  }
  void notice(std::string_view n) override { emit("notice", {{"text", n}}); }

 private:
  net::Connection& c_;
  Bus& bus_;
  std::string sid_;
  bool open_ = true;
};

class Server {
 public:
  Server(cli::App& app, const Options& opts) : app_(app), opts_(opts) {}

  int run() {
    auto l = net::Listener::bind(opts_.host, opts_.port);
    if (!l) return std::cerr << "error: " << l.error().message << "\n", 1;
    auto url = std::format("http://{}:{}", opts_.host, (*l)->port());
    std::cout << "shaman server listening on " << url << std::endl;
    if (opts_.on_listen) opts_.on_listen(url);
    while (true) {
      auto conn = (*l)->accept();
      if (!conn) continue;
      std::thread([this, c = std::shared_ptr<net::Connection>(std::move(conn))] { handle(*c); }).detach();
    }
  }

 private:
  Live& live(const std::string& id, std::atomic<bool>** cancel_out = nullptr) {
    std::lock_guard lock(mu_);
    auto& l = sessions_[id];
    if (!l) {
      l = std::make_unique<Live>();
      auto asker = [this, id](const permission::Request& r) { return ask(id, r); };
      auto services = app_.services(asker, &l->cancel, opts_.allow_all);
      l->runner = std::make_unique<session::Runner>(services);
    }
    if (cancel_out) *cancel_out = &l->cancel;
    return *l;
  }

  permission::Reply ask(const std::string& session, const permission::Request& r) {
    auto id = make_id("per");
    {
      std::lock_guard lock(pending_.mu);
      pending_.replies[id] = std::nullopt;
    }
    Json ev = {{"id", id}, {"session", session}, {"permission", r.permission}, {"subject", r.subject}, {"title", r.title}};
    if (current_stream_) current_stream_->emit("permission", ev);
    else bus_.publish("permission", ev);
    std::unique_lock lock(pending_.mu);
    bool answered = pending_.cv.wait_for(lock, 10min, [&] { return pending_.replies[id].has_value(); });
    auto reply = answered ? *pending_.replies[id] : permission::Reply::reject;
    pending_.replies.erase(id);
    return reply;
  }

  void handle(net::Connection& c) {
    auto req = c.read_request();
    if (!req) return;
    log::debug(log::Cat::http, "server {} {}", req->method, req->path);
    if (req->method == "OPTIONS") {
      c.respond(204, "text/plain", "", {{"Access-Control-Allow-Methods", "GET, POST, DELETE"},
                                        {"Access-Control-Allow-Headers", "Content-Type, Authorization"}});
      return;
    }
    bool page = req->method == "GET" && (req->path == "/" || req->path == "/index.html");  // static; API calls still need the token
    if (!page && !opts_.token.empty() && req->header("authorization") != "Bearer " + opts_.token) {
      c.json(401, R"({"error":"unauthorized"})");
      return;
    }
    Json body = Json::object();
    if (!req->body.empty()) {
      try {
        body = Json::parse(req->body);
      } catch (...) {
        c.json(400, R"({"error":"invalid JSON body"})");
        return;
      }
    }
    try {
      route(c, *req, body);
    } catch (const std::exception& e) {
      c.json(500, Json{{"error", e.what()}}.dump());
    }
  }

  void route(net::Connection& c, const net::Request& req, const Json& body) {
    static const std::regex session_re(R"(^/session/([A-Za-z0-9_]+)(/[a-z]+)?$)");
    static const std::regex permission_re(R"(^/permission/([A-Za-z0-9_]+)$)");
    std::smatch m;
    auto& M = req.method;

    if (M == "GET" && (req.path == "/" || req.path == "/index.html"))
      return void(c.respond(200, "text/html; charset=utf-8", embedded::web_index));
    if (M == "GET" && req.path == "/health") return void(c.json(200, Json{{"version", kVersion}}.dump()));
    if (M == "GET" && req.path == "/config") return void(c.json(200, app_.config.raw.dump()));
    if (M == "GET" && req.path == "/models") {
      Json out = Json::array();
      for (auto& p : app_.providers->providers()) {
        if (!app_.providers->usable(p)) continue;
        for (auto& mdl : app_.providers->visible_models(p))
          out.push_back({{"id", p.id + "/" + mdl.id}, {"name", mdl.name}, {"free", mdl.free()}, {"context", mdl.context}});
      }
      return void(c.json(200, out.dump()));
    }
    if (M == "GET" && req.path == "/agents") {
      Json out = Json::array();
      for (auto* a : app_.agents->list())
        out.push_back({{"name", a->name}, {"description", a->description}, {"mode", a->mode == agent::Mode::subagent ? "subagent" : "primary"}});
      return void(c.json(200, out.dump()));
    }
    if (M == "GET" && req.path == "/commands") {
      Json out = Json::array();
      for (auto& cmd : command::discover(app_.config, app_.root)) out.push_back({{"name", cmd.name}, {"description", cmd.description}});
      return void(c.json(200, out.dump()));
    }
    if (M == "GET" && req.path == "/event") {
      c.start_sse();
      {
        std::lock_guard lock(bus_.mu);
        bus_.subscribers.push_back(&c);
      }
      while (true) {  // keep-alive until the client goes away
        std::this_thread::sleep_for(15s);
        std::lock_guard lock(bus_.mu);
        if (!c.write(": ping\n\n")) {
          bus_.subscribers.remove(&c);
          return;
        }
      }
    }
    if (req.path == "/session") {
      if (M == "GET") {
        Json out = Json::array();
        for (auto& s : app_.store->list()) out.push_back(session::to_json(s));
        return void(c.json(200, out.dump()));
      }
      if (M == "POST") {
        auto s = app_.store->create(body.value("agent", app_.config.default_agent), body.value("model", ""));
        if (!s) return void(c.json(500, Json{{"error", s.error().message}}.dump()));
        return void(c.json(201, session::to_json(*s).dump()));
      }
    }
    if (std::regex_match(req.path, m, permission_re) && M == "POST") {
      auto r = body.value("reply", "reject");
      std::lock_guard lock(pending_.mu);
      auto it = pending_.replies.find(m[1]);
      if (it == pending_.replies.end()) return void(c.json(404, R"({"error":"no such permission request"})"));
      it->second = r == "always" ? permission::Reply::always : r == "once" ? permission::Reply::once : permission::Reply::reject;
      pending_.cv.notify_all();
      return void(c.json(200, R"({"ok":true})"));
    }
    if (std::regex_match(req.path, m, session_re)) {
      std::string id = m[1], action = m[2];
      auto info = app_.store->get(id);
      if (!info) return void(c.json(404, Json{{"error", info.error().message}}.dump()));
      if (action.empty() && M == "GET") return void(c.json(200, session::to_json(*info).dump()));
      if (action.empty() && M == "DELETE") {
        std::error_code ec;
        std::filesystem::remove_all(app_.store->dir() / id, ec);
        return void(c.json(200, R"({"ok":true})"));
      }
      if (action == "/message" && M == "GET") {
        Json out = Json::array();
        if (auto ms = app_.store->messages(id))
          for (auto& msg : *ms) out.push_back(llm::to_json(msg));
        return void(c.json(200, out.dump()));
      }
      if (action == "/abort" && M == "POST") {
        live(id).cancel = true;
        return void(c.json(200, R"({"ok":true})"));
      }
      if (action == "/message" && M == "POST") return prompt(c, *info, body);
      if ((action == "/undo" || action == "/compact") && M == "POST") {
        auto& l = live(id);
        std::unique_lock busy(l.busy, std::try_to_lock);
        if (!busy) return void(c.json(409, R"({"error":"session is busy"})"));
        if (action == "/undo") {
          auto r = l.runner->undo(*info);
          return void(c.json(r ? 200 : 400, r ? Json{{"changes", *r}}.dump() : Json{{"error", r.error().message}}.dump()));
        }
        session::Events quiet;
        auto r = l.runner->compact(*info, quiet);
        return void(c.json(r ? 200 : 500, r ? R"({"ok":true})" : Json{{"error", r.error().message}}.dump()));
      }
    }
    c.json(404, R"({"error":"not found"})");
  }

  void prompt(net::Connection& c, session::Info info, const Json& body) {
    auto text = body.value("text", "");
    if (text.empty()) return void(c.json(400, R"({"error":"text is required"})"));
    auto& l = live(info.id);
    std::unique_lock busy(l.busy, std::try_to_lock);
    if (!busy) return void(c.json(409, R"({"error":"session is busy"})"));
    if (body.contains("agent")) info.agent = body["agent"];
    session::PromptOptions po;
    if (body.contains("model")) po.model = body["model"].get<std::string>();
    po.attachments = body.value("files", std::vector<std::string>{});
    l.cancel = false;
    c.start_sse();
    StreamEvents ev(c, bus_, info.id);
    current_stream_ = &ev;
    auto r = l.runner->prompt(info, text, ev, po);
    current_stream_ = nullptr;
    if (r) ev.emit("done", {{"text", *r}, {"cost", info.cost}, {"input_tokens", info.usage.input}, {"output_tokens", info.usage.output}});
    else ev.emit("error", {{"message", r.error().message}});
  }

  cli::App& app_;
  Options opts_;
  std::mutex mu_;
  std::map<std::string, std::unique_ptr<Live>> sessions_;
  Pending pending_;
  Bus bus_;
  // The runner asks for permission on the request's own thread, so the
  // question can go straight to that request's stream.
  static thread_local StreamEvents* current_stream_;
};

thread_local StreamEvents* Server::current_stream_ = nullptr;

}  // namespace

int serve(cli::App& app, const Options& opts) { return Server(app, opts).run(); }

}  // namespace shaman::server
