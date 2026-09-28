#include "shaman/acp/acp.hpp"

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

#include "shaman/cli/app.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/version.hpp"

namespace shaman::acp {
namespace {

using namespace std::chrono_literals;

std::string tool_kind(const std::string& tool) {
  if (tool == "read" || tool == "list") return "read";
  if (tool == "edit" || tool == "write" || tool == "apply_patch") return "edit";
  if (tool == "grep" || tool == "glob" || tool == "websearch" || tool == "lsp") return "search";
  if (tool == "bash") return "execute";
  if (tool == "webfetch") return "fetch";
  if (tool == "todowrite" || tool == "todoread" || tool == "task") return "think";
  return "other";
}

class Agent;

struct Session {
  std::string id;
  cli::App* app = nullptr;
  std::unique_ptr<session::Runner> runner;
  std::atomic<bool> cancel{false};
  std::mutex busy;
};

class Updates final : public session::Events {
 public:
  Updates(Agent& a, std::string sid) : a_(a), sid_(std::move(sid)) {}
  void text(std::string_view t) override;
  void reasoning(std::string_view t) override;
  void tool_start(const llm::ToolCallPart& c) override;
  void tool_end(const llm::ToolCallPart& c, const tool::Output& o) override;
  void step_end(int, const llm::Usage& u, const std::string&) override;
  void notice(std::string_view n) override;

 private:
  Agent& a_;
  std::string sid_;
};

class Agent {
 public:
  Agent(std::filesystem::path cwd, Options opts) : cwd_(std::move(cwd)), opts_(opts) {}

  void send(const Json& msg) {
    std::lock_guard lock(out_mu_);
    std::cout << msg.dump() << "\n" << std::flush;
  }
  void reply(const Json& id, Json result) { send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}); }
  void error(const Json& id, int code, const std::string& msg) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}});
  }
  void update(const std::string& sid, Json u) {
    send({{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", {{"sessionId", sid}, {"update", std::move(u)}}}});
  }

  // Agent -> client request; blocks the calling (worker) thread for the answer.
  Json request(const std::string& method, const Json& params) {
    int id;
    {
      std::lock_guard lock(req_mu_);
      id = next_id_++;
      responses_[id] = std::nullopt;
    }
    send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
    std::unique_lock lock(req_mu_);
    req_cv_.wait_for(lock, 30min, [&] { return responses_[id].has_value(); });
    auto r = responses_[id].value_or(Json());
    responses_.erase(id);
    return r;
  }

  int run() {
    std::string line;
    while (std::getline(std::cin, line)) {
      Json msg;
      try {
        msg = Json::parse(line);
      } catch (...) {
        continue;
      }
      log::debug(log::Cat::session, "acp <- {}", line.substr(0, 300));
      if (!msg.contains("method") && msg.contains("id")) {  // response to our request
        std::lock_guard lock(req_mu_);
        if (msg["id"].is_number_integer()) responses_[msg["id"].get<int>()] = msg.value("result", msg.value("error", Json()));
        req_cv_.notify_all();
        continue;
      }
      handle(msg);
    }
    for (auto& t : workers_) t.join();
    return 0;
  }

 private:
  cli::App* app_for(const std::string& cwd) {
    auto key = cwd.empty() ? cwd_.string() : cwd;
    auto& app = apps_[key];
    if (!app) {
      auto a = cli::App::create(key, true);
      if (!a) return nullptr;
      app = std::move(*a);
    }
    return app.get();
  }

  Session& open(const std::string& id, cli::App* app) {
    std::lock_guard lock(sess_mu_);
    auto& s = sessions_[id];
    if (!s) {
      s = std::make_unique<Session>();
      s->id = id;
      s->app = app;
      auto asker = [this, id](const permission::Request& r) { return ask(id, r); };
      s->runner = std::make_unique<session::Runner>(app->services(asker, &s->cancel, opts_.allow_all));
    }
    return *s;
  }

  permission::Reply ask(const std::string& sid, const permission::Request& r) {
    auto res = request("session/request_permission",
                       {{"sessionId", sid},
                        {"toolCall", {{"toolCallId", "perm_" + r.permission}, {"title", r.title}, {"kind", tool_kind(r.permission)},
                                      {"status", "pending"}, {"rawInput", {{"subject", r.subject}}}}},
                        {"options", {{{"optionId", "once"}, {"kind", "allow_once"}, {"name", "Allow once"}},
                                     {{"optionId", "always"}, {"kind", "allow_always"}, {"name", "Always allow"}},
                                     {{"optionId", "reject"}, {"kind", "reject_once"}, {"name", "Reject"}}}}});
    auto outcome = res.value("outcome", Json::object());
    auto option = outcome.value("optionId", "");
    if (outcome.value("outcome", "") != "selected") return permission::Reply::reject;
    return option == "always" ? permission::Reply::always : option == "once" ? permission::Reply::once : permission::Reply::reject;
  }

  void add_mcp(cli::App* app, const Json& servers) {
    Config cfg = app->config;
    cfg.mcp.clear();
    for (auto& s : servers) {
      McpServerConfig m;
      if (s.contains("url")) {
        m.type = "remote";
        m.url = s["url"];
        for (auto& h : s.value("headers", Json::array())) m.headers[h.value("name", "")] = h.value("value", "");
      } else {
        m.command.push_back(s.value("command", ""));
        for (auto& a : s.value("args", Json::array())) m.command.push_back(a);
        for (auto& e : s.value("env", Json::array())) m.environment[e.value("name", "")] = e.value("value", "");
      }
      auto name = s.value("name", "mcp");
      bool known = false;
      for (auto* t : app->tools.all()) known = known || t->name().starts_with(name + "_");
      if (!known) cfg.mcp[name] = m;
    }
    for (auto& c : mcp::load(cfg, app->tools)) app->mcp.push_back(c);
  }

  Json modes(cli::App* app, const std::string& current) {
    Json available = Json::array();
    for (auto* a : app->agents->list())
      if (a->mode != agent::Mode::subagent) available.push_back({{"id", a->name}, {"name", a->name}, {"description", a->description}});
    return {{"currentModeId", current}, {"availableModes", available}};
  }

  void handle(const Json& msg) {
    auto method = msg.value("method", "");
    auto params = msg.value("params", Json::object());
    Json id = msg.value("id", Json());
    if (method == "initialize") {
      return reply(id, {{"protocolVersion", 1},
                        {"agentCapabilities", {{"loadSession", true},
                                               {"mcpCapabilities", {{"http", true}, {"sse", false}}},
                                               {"promptCapabilities", {{"image", true}, {"embeddedContext", true}}},
                                               {"sessionCapabilities", {{"list", Json::object()}}}}},
                        {"authMethods", Json::array()},
                        {"agentInfo", {{"name", "shaman"}, {"version", kVersion}}}});
    }
    if (method == "authenticate") return reply(id, Json::object());
    if (method == "session/new" || method == "session/load") {
      auto* app = app_for(params.value("cwd", ""));
      if (!app) return error(id, -32603, "cannot load project");
      add_mcp(app, params.value("mcpServers", Json::array()));
      Result<session::Info> info = method == "session/new"
                                       ? app->store->create(app->config.default_agent, "")
                                       : app->store->get(params.value("sessionId", ""));
      if (!info) return error(id, -32602, info.error().message);
      open(info->id, app);
      if (method == "session/load") {  // replay history as updates
        if (auto ms = app->store->messages(info->id))
          for (auto& m : *ms)
            for (auto& p : m.parts)
              if (auto* t = std::get_if<llm::TextPart>(&p))
                update(info->id, {{"sessionUpdate", m.role == llm::Role::user ? "user_message_chunk" : "agent_message_chunk"},
                                  {"content", {{"type", "text"}, {"text", t->text}}}});
        return reply(id, {{"modes", modes(app, info->agent)}});
      }
      return reply(id, {{"sessionId", info->id}, {"modes", modes(app, info->agent)}});
    }
    if (method == "session/list") {
      auto* app = app_for(params.value("cwd", ""));
      Json list = Json::array();
      if (app)
        for (auto& s : app->store->list())
          list.push_back({{"sessionId", s.id}, {"title", s.title}, {"cwd", app->root.string()}, {"updatedAt", s.updated}});
      return reply(id, {{"sessions", list}});
    }
    if (method == "session/cancel") {
      std::lock_guard lock(sess_mu_);
      if (auto it = sessions_.find(params.value("sessionId", "")); it != sessions_.end()) it->second->cancel = true;
      return;
    }
    if (method == "session/set_mode") {
      auto sid = params.value("sessionId", "");
      std::lock_guard lock(sess_mu_);
      auto it = sessions_.find(sid);
      if (it == sessions_.end()) return error(id, -32602, "unknown session");
      auto info = it->second->app->store->get(sid);
      if (info) {
        info->agent = params.value("modeId", info->agent);
        it->second->app->store->save(*info);
      }
      return reply(id, Json::object());
    }
    if (method == "session/prompt") return prompt(id, params);
    if (!id.is_null()) error(id, -32601, "method not found: " + method);
  }

  void prompt(const Json& id, const Json& params) {
    auto sid = params.value("sessionId", "");
    Session* s;
    {
      std::lock_guard lock(sess_mu_);
      auto it = sessions_.find(sid);
      if (it == sessions_.end()) return error(id, -32602, "unknown session");
      s = it->second.get();
    }
    std::string text;
    std::vector<std::string> files;
    for (auto& block : params.value("prompt", Json::array())) {
      auto type = block.value("type", "");
      if (type == "text") text += block.value("text", "");
      else if (type == "resource_link") files.push_back(lsp_path(block.value("uri", "")));
      else if (type == "resource") {
        auto& r = block["resource"];
        text += "\n\n<file path=\"" + lsp_path(r.value("uri", "")) + "\">\n" + r.value("text", "") + "\n</file>";
      } else if (type == "image") {
        auto path = s->app->store->dir() / (sid + "-image." + str::split(block.value("mimeType", "image/png"), '/').back());
        std::ofstream(path, std::ios::binary) << decode_base64(block.value("data", ""));
        files.push_back(path.string());
      }
    }
    workers_.emplace_back([this, id, s, text, files] {
      std::lock_guard busy(s->busy);
      s->cancel = false;
      auto info = s->app->store->get(s->id);
      if (!info) return error(id, -32603, info.error().message);
      Updates ev(*this, s->id);
      session::PromptOptions po;
      po.attachments = files;
      auto r = s->runner->prompt(*info, text, ev, po);
      if (!r && !s->cancel) {
        update(s->id, {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", "\n\nError: " + r.error().message}}}});
      }
      reply(id, {{"stopReason", s->cancel ? "cancelled" : "end_turn"}});
    });
  }

  static std::string lsp_path(const std::string& uri) { return uri.starts_with("file://") ? str::url_decode(uri.substr(7)) : uri; }

  static std::string decode_base64(const std::string& in) {
    static const std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (char c : in) {
      auto pos = tbl.find(c);
      if (pos == std::string::npos) continue;
      val = (val << 6) + int(pos);
      bits += 6;
      if (bits >= 0) {
        out += char((val >> bits) & 0xFF);
        bits -= 8;
      }
    }
    return out;
  }

  std::filesystem::path cwd_;
  Options opts_;
  std::mutex out_mu_, req_mu_, sess_mu_;
  std::condition_variable req_cv_;
  int next_id_ = 1;
  std::map<int, std::optional<Json>> responses_;
  std::map<std::string, std::unique_ptr<cli::App>> apps_;
  std::map<std::string, std::unique_ptr<Session>> sessions_;
  std::vector<std::thread> workers_;

  friend class Updates;
};

void Updates::text(std::string_view t) {
  a_.update(sid_, {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", t}}}});
}
void Updates::reasoning(std::string_view t) {
  a_.update(sid_, {{"sessionUpdate", "agent_thought_chunk"}, {"content", {{"type", "text"}, {"text", t}}}});
}
void Updates::tool_start(const llm::ToolCallPart& c) {
  Json u = {{"sessionUpdate", "tool_call"}, {"toolCallId", c.id}, {"title", c.name}, {"kind", tool_kind(c.name)},
            {"status", "in_progress"}, {"rawInput", c.input}};
  if (c.input.contains("filePath")) u["locations"] = Json::array({{{"path", c.input["filePath"]}}});
  a_.update(sid_, u);
}
void Updates::tool_end(const llm::ToolCallPart& c, const tool::Output& o) {
  a_.update(sid_, {{"sessionUpdate", "tool_call_update"}, {"toolCallId", c.id}, {"title", o.title},
                   {"status", o.is_error ? "failed" : "completed"},
                   {"content", {{{"type", "content"}, {"content", {{"type", "text"}, {"text", o.text.substr(0, 20000)}}}}}}});
  if (c.name == "todowrite") {
    Json entries = Json::array();
    for (auto& t : c.input.value("todos", Json::array()))
      entries.push_back({{"content", t.value("content", "")}, {"priority", t.value("priority", "medium")},
                         {"status", t.value("status", "pending") == "cancelled" ? "completed" : t.value("status", "pending")}});
    a_.update(sid_, {{"sessionUpdate", "plan"}, {"entries", entries}});
  }
}
void Updates::step_end(int, const llm::Usage& u, const std::string&) {
  a_.update(sid_, {{"sessionUpdate", "usage_update"}, {"inputTokens", u.input}, {"outputTokens", u.output}});
}
void Updates::notice(std::string_view n) {
  a_.update(sid_, {{"sessionUpdate", "agent_thought_chunk"}, {"content", {{"type", "text"}, {"text", std::string(n) + "\n"}}}});
}

}  // namespace

int serve(const std::filesystem::path& cwd, const Options& opts) { return Agent(cwd, opts).run(); }

}  // namespace shaman::acp
