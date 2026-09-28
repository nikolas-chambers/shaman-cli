#include "shaman/mcp/mcp.hpp"

#include <iostream>

#include "shaman/core/log.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"
#include "shaman/http/sse.hpp"
#include "shaman/mcp/oauth.hpp"
#include "shaman/version.hpp"

namespace shaman::mcp {
using namespace std::chrono_literals;

constexpr const char* kProtocol = "2025-06-18";

namespace {

class StdioTransport final : public Transport {
 public:
  StdioTransport(std::string name, process::Child child) : name_(std::move(name)), child_(std::move(child)) {}

  Result<Json> request(const Json& msg, std::chrono::milliseconds timeout) override {
    if (auto r = child_.write_line(msg.dump()); !r) return std::unexpected(r.error());
    while (true) {
      auto line = child_.read_line(timeout);
      if (!line) return std::unexpected(line.error());
      if (!*line) return fail("mcp " + name_ + ": timed out waiting for " + msg.value("method", ""));
      log::debug(log::Cat::mcp, "{} <- {}", name_, **line);
      Json reply;
      try {
        reply = Json::parse(**line);
      } catch (...) {
        continue;  // stray output
      }
      if (reply.contains("id") && reply["id"] == msg["id"] && !reply.contains("method")) return reply;
      if (reply.contains("id") && reply.contains("method"))  // server->client request (e.g. ping): answer
        child_.write_line(Json{{"jsonrpc", "2.0"}, {"id", reply["id"]}, {"result", Json::object()}}.dump());
    }
  }
  void notify(const Json& msg) override { child_.write_line(msg.dump()); }

 private:
  std::string name_;
  process::Child child_;
};

// Streamable HTTP: POST each message; the reply is JSON or an SSE stream.
class HttpTransport final : public Transport {
 public:
  HttpTransport(std::string name, McpServerConfig cfg) : name_(std::move(name)), cfg_(std::move(cfg)) {}

  Result<Json> request(const Json& msg, std::chrono::milliseconds timeout) override {
    auto res = post(msg, timeout);
    if (!res) return std::unexpected(res.error());
    if (res->status == 401)
      return fail(std::format("mcp {}: unauthorised; run `shaman mcp auth {}`", name_, name_), 401);
    if (res->status >= 300) return fail(std::format("mcp {}: HTTP {}: {}", name_, res->status, res->body.substr(0, 300)), int(res->status));
    if (auto sid = res->header("mcp-session-id"); !sid.empty()) session_ = sid;
    auto type = res->header("content-type");
    if (type.find("text/event-stream") != std::string::npos) {
      http::SseParser parser;
      std::optional<Json> reply;
      parser.feed(res->body + "\n\n", [&](const http::SseEvent& ev) {
        try {
          auto j = Json::parse(ev.data);
          if (j.contains("id") && j["id"] == msg["id"] && !j.contains("method")) reply = j;
        } catch (...) {
        }
      });
      if (!reply) return fail("mcp " + name_ + ": no response in event stream");
      return *reply;
    }
    try {
      return Json::parse(res->body);
    } catch (...) {
      return fail("mcp " + name_ + ": response is not JSON");
    }
  }

  void notify(const Json& msg) override { post(msg, 10s); }

 private:
  Result<http::Response> post(const Json& msg, std::chrono::milliseconds timeout) {
    http::Request req;
    req.method = "POST";
    req.url = cfg_.url;
    req.body = msg.dump();
    req.timeout_s = long(std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
    req.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json, text/event-stream"},
                   {"MCP-Protocol-Version", kProtocol}};
    if (!session_.empty()) req.headers.emplace_back("Mcp-Session-Id", session_);
    bool has_auth = false;
    for (auto& [k, v] : cfg_.headers) {
      req.headers.emplace_back(k, v);
      has_auth = has_auth || str_ieq(k, "authorization");
    }
    if (!has_auth && cfg_.oauth != false)
      if (auto token = oauth::access_token(name_, cfg_)) req.headers.emplace_back("Authorization", "Bearer " + *token);
    log::debug(log::Cat::mcp, "{} -> {}", name_, req.body.substr(0, 300));
    return http::send(req);
  }
  static bool str_ieq(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
  }

  std::string name_;
  McpServerConfig cfg_;
  std::string session_;
};

class McpTool final : public tool::Tool {
 public:
  McpTool(std::shared_ptr<Client> c, Json spec) : client_(std::move(c)), spec_(std::move(spec)) {}
  std::string name() const override { return client_->name() + "_" + spec_.value("name", ""); }
  std::string description() const override { return spec_.value("description", ""); }
  Json schema() const override { return spec_.value("inputSchema", Json{{"type", "object"}}); }
  tool::Output run(const Json& input, tool::Context& ctx) override {
    if (!ctx.permit("mcp", name(), "MCP " + name())) return tool::error("permission denied");
    auto r = client_->call("tools/call", {{"name", spec_.value("name", "")}, {"arguments", input}});
    if (!r) return tool::error(r.error().message);
    std::string text;
    for (auto& c : r->value("content", Json::array()))
      text += c.value("type", "") == "text" ? c.value("text", "") + "\n" : "[" + c.value("type", "?") + " content]\n";
    return {tool::truncate(text), r->value("isError", false), name()};
  }

 private:
  std::shared_ptr<Client> client_;
  Json spec_;
};

// Resources that MCP servers expose (files, docs, records): list them, or read one by URI.
class McpResources final : public tool::Tool {
 public:
  explicit McpResources(std::vector<std::shared_ptr<Client>> clients) : clients_(std::move(clients)) {}
  std::string name() const override { return "mcp_resources"; }
  std::string description() const override {
    return "List or read resources exposed by connected MCP servers (documents, files, records). Call with no uri to "
           "list them (optionally for one server), or with a uri to read it.";
  }
  Json schema() const override {
    return {{"type", "object"}, {"properties", {{"server", {{"type", "string"}}}, {"uri", {{"type", "string"}}}}}};
  }
  tool::Output run(const Json& in, tool::Context& ctx) override {
    auto server = in.value("server", ""), uri = in.value("uri", "");
    if (!ctx.permit("mcp", "mcp_resources", uri.empty() ? "List MCP resources" : "Read MCP resource " + uri))
      return tool::error("permission denied");
    std::string out;
    for (auto& c : clients_) {
      if (!server.empty() && c->name() != server) continue;
      if (!c->has("resources")) continue;
      if (!uri.empty()) {
        auto r = c->read_resource(uri);
        if (r) return {tool::truncate(*r), false, "Read " + uri};
        if (!server.empty()) return tool::error(r.error().message);
        continue;  // try the next server
      }
      auto list = c->list_resources();
      if (!list) continue;
      for (auto& r : *list)
        out += std::format("{}  {}  {}\n", c->name(), r.value("uri", ""), r.value("name", r.value("description", "")));
    }
    if (!uri.empty()) return tool::error("no server could read " + uri);
    return {out.empty() ? "no resources" : out, false, "MCP resources"};
  }

 private:
  std::vector<std::shared_ptr<Client>> clients_;
};

}  // namespace

Result<std::shared_ptr<Client>> Client::connect(const std::string& name, const McpServerConfig& cfg) {
  std::unique_ptr<Transport> transport;
  if (cfg.type == "remote") {
    if (cfg.url.empty()) return fail("remote server needs a url");
    transport = std::make_unique<HttpTransport>(name, cfg);
  } else {
    std::vector<std::pair<std::string, std::string>> env(cfg.environment.begin(), cfg.environment.end());
    auto child = process::Child::spawn(cfg.command, env);
    if (!child) return std::unexpected(child.error());
    transport = std::make_unique<StdioTransport>(name, std::move(*child));
  }
  auto client = std::shared_ptr<Client>(new Client(name, std::move(transport), std::chrono::milliseconds(cfg.timeout_ms)));
  auto init = client->call("initialize", {{"protocolVersion", kProtocol}, {"capabilities", Json::object()},
                                          {"clientInfo", {{"name", "shaman-cli"}, {"version", kVersion}}}});
  if (!init) return std::unexpected(init.error());
  client->capabilities_ = init->value("capabilities", Json::object());
  client->transport_->notify(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
  return client;
}

Result<Json> Client::call(const std::string& method, const Json& params) {
  std::lock_guard lock(mu_);
  Json msg = {{"jsonrpc", "2.0"}, {"id", next_id_++}, {"method", method}, {"params", params}};
  log::debug(log::Cat::mcp, "{} -> {}", name_, msg.dump().substr(0, 300));
  auto reply = transport_->request(msg, timeout_);
  if (!reply) return std::unexpected(reply.error());
  if (reply->contains("error")) return fail("mcp " + name_ + ": " + (*reply)["error"].value("message", "error"));
  return reply->value("result", Json::object());
}

Result<std::vector<Json>> Client::list_all(const std::string& method, const std::string& key) {
  std::vector<Json> all;
  Json params = Json::object();
  while (true) {  // follow pagination cursors
    auto r = call(method, params);
    if (!r) return std::unexpected(r.error());
    for (auto& t : r->value(key, Json::array())) all.push_back(t);
    auto cursor = r->value("nextCursor", "");
    if (cursor.empty()) break;
    params["cursor"] = cursor;
  }
  return all;
}

Result<std::vector<Json>> Client::list_tools() { return list_all("tools/list", "tools"); }

Result<std::vector<Json>> Client::list_prompts() {
  if (!has("prompts")) return std::vector<Json>{};
  return list_all("prompts/list", "prompts");
}

Result<std::vector<Json>> Client::list_resources() {
  if (!has("resources")) return std::vector<Json>{};
  return list_all("resources/list", "resources");
}

static std::string content_text(const Json& c) {
  if (!c.is_object()) return "";
  auto type = c.value("type", "");
  if (type == "text") return c.value("text", "");
  if (type == "resource") return c.value("resource", Json::object()).value("text", "");
  return "[" + type + " content omitted]";
}

Result<std::string> Client::get_prompt(const std::string& prompt, const Json& arguments) {
  auto r = call("prompts/get", {{"name", prompt}, {"arguments", arguments}});
  if (!r) return std::unexpected(r.error());
  std::string out;
  for (auto& m : r->value("messages", Json::array())) {
    auto text = content_text(m.value("content", Json::object()));
    if (text.empty()) continue;
    if (!out.empty()) out += "\n\n";
    out += m.value("role", "user") == "assistant" ? "(assistant) " + text : text;
  }
  return out;
}

Result<std::string> Client::read_resource(const std::string& uri) {
  auto r = call("resources/read", {{"uri", uri}});
  if (!r) return std::unexpected(r.error());
  std::string out;
  for (auto& c : r->value("contents", Json::array())) {
    if (!out.empty()) out += "\n\n";
    out += c.contains("text") ? c.value("text", "") : "[binary " + c.value("mimeType", "data") + " omitted]";
  }
  return out;
}

std::vector<command::Command> prompt_commands(const std::vector<std::shared_ptr<Client>>& clients) {
  std::vector<command::Command> out;
  for (auto& client : clients) {
    auto prompts = client->list_prompts();
    if (!prompts) continue;
    for (auto& p : *prompts) {
      command::Command cmd;
      auto pname = p.value("name", "");
      cmd.name = client->name() + ":" + pname;
      cmd.description = p.value("description", "MCP prompt");
      cmd.source = "mcp " + client->name();
      std::vector<std::string> names;
      for (auto& a : p.value("arguments", Json::array())) names.push_back(a.value("name", ""));
      cmd.fetch = [client, pname, names](const std::string& args) -> std::string {
        Json values = Json::object();
        std::vector<std::string> positional;
        for (auto& word : str::split(args, ' ')) {
          if (word.empty()) continue;
          auto eq = word.find('=');
          if (eq != std::string::npos && std::ranges::find(names, word.substr(0, eq)) != names.end())
            values[word.substr(0, eq)] = word.substr(eq + 1);
          else
            positional.push_back(word);
        }
        size_t i = 0;
        for (auto& n : names) {
          if (values.contains(n) || i >= positional.size()) continue;
          bool last = &n == &names.back();
          std::string v = positional[i++];
          if (last) while (i < positional.size()) v += " " + positional[i++];  // the last argument takes the rest
          values[n] = v;
        }
        auto r = client->get_prompt(pname, values);
        return r ? *r : "(MCP prompt " + pname + " failed: " + r.error().message + ")";
      };
      out.push_back(std::move(cmd));
    }
  }
  return out;
}

std::vector<std::shared_ptr<Client>> load(const Config& config, tool::Registry& tools) {
  std::vector<std::shared_ptr<Client>> clients;
  for (auto& [name, cfg] : config.mcp) {
    if (!cfg.enabled || (cfg.command.empty() && cfg.url.empty())) continue;
    auto client = Client::connect(name, cfg);
    if (!client) {
      log::warn("mcp " + name + ": " + client.error().message);
      continue;
    }
    auto list = (*client)->list_tools();
    if (!list) {
      log::warn("mcp " + name + ": " + list.error().message);
      continue;
    }
    for (auto& spec : *list) tools.add(std::make_unique<McpTool>(*client, spec));
    log::debug(log::Cat::mcp, "{}: {} tools", name, list->size());
    clients.push_back(*client);
  }
  if (std::ranges::any_of(clients, [](auto& c) { return c->has("resources"); }))
    tools.add(std::make_unique<McpResources>(clients));
  return clients;
}

int serve(const Config& config, const std::filesystem::path& root, tool::Registry& tools, bool allow_all) {
  // The MCP client owns the user interaction, so "ask" rules cannot prompt
  // here: they are refused unless --yolo was given.
  auto rules = permission::Rules::defaults();
  rules.push(permission::Rules::from_json(config.permission));
  permission::Gate gate(std::move(rules), nullptr);
  if (allow_all) gate.allow_all();
  std::set<std::filesystem::path> read;
  std::vector<tool::Todo> todos;
  const std::set<std::string> exposed{"read", "write", "edit", "apply_patch", "list", "glob", "grep",
                                      "bash", "webfetch", "websearch", "lsp", "skill"};
  auto reply = [](const Json& id, Json result) {
    std::cout << Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}.dump() << "\n" << std::flush;
  };
  std::string line;
  while (std::getline(std::cin, line)) {
    Json msg;
    try {
      msg = Json::parse(line);
    } catch (...) {
      continue;
    }
    if (!msg.contains("id")) continue;  // notifications need no reply
    auto method = msg.value("method", "");
    auto params = msg.value("params", Json::object());
    if (method == "initialize") {
      reply(msg["id"], {{"protocolVersion", params.value("protocolVersion", kProtocol)},
                        {"capabilities", {{"tools", Json::object()}}},
                        {"serverInfo", {{"name", "shaman"}, {"version", kVersion}}}});
    } else if (method == "ping") {
      reply(msg["id"], Json::object());
    } else if (method == "tools/list") {
      Json list = Json::array();
      for (auto* t : tools.all())
        if (exposed.contains(t->name()))
          list.push_back({{"name", t->name()}, {"description", t->description()}, {"inputSchema", t->schema()}});
      reply(msg["id"], {{"tools", list}});
    } else if (method == "tools/call") {
      auto* t = tools.find(params.value("name", ""));
      if (!t || !exposed.contains(t->name())) {
        reply(msg["id"], {{"content", {{{"type", "text"}, {"text", "unknown tool"}}}}, {"isError", true}});
        continue;
      }
      tool::Context ctx{root, "mcp", &gate, nullptr, &read, &todos, nullptr};
      auto out = t->run(params.value("arguments", Json::object()), ctx);
      reply(msg["id"], {{"content", {{{"type", "text"}, {"text", out.text}}}}, {"isError", out.is_error}});
    } else {
      std::cout << Json{{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"error", {{"code", -32601}, {"message", "method not found"}}}}.dump()
                << "\n" << std::flush;
    }
  }
  return 0;
}

}  // namespace shaman::mcp
