#include "shaman/mcp/mcp.hpp"

#include "shaman/core/log.hpp"

namespace shaman::mcp {
using namespace std::chrono_literals;

Result<std::shared_ptr<Client>> Client::connect(const std::string& name, const McpServerConfig& cfg) {
  std::vector<std::pair<std::string, std::string>> env(cfg.environment.begin(), cfg.environment.end());
  auto child = process::Child::spawn(cfg.command, env);
  if (!child) return std::unexpected(child.error());
  auto client = std::shared_ptr<Client>(new Client(name, std::move(*child)));
  auto init = client->call("initialize", {{"protocolVersion", "2025-06-18"},
                                          {"capabilities", Json::object()},
                                          {"clientInfo", {{"name", "shaman-cli"}, {"version", "0.1.0"}}}});
  if (!init) return std::unexpected(init.error());
  client->child_.write_line(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}.dump());
  return client;
}

Result<Json> Client::call(const std::string& method, const Json& params) {
  int id = next_id_++;
  Json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
  log::debug(log::Cat::mcp, "{} -> {}", name_, msg.dump());
  if (auto r = child_.write_line(msg.dump()); !r) return std::unexpected(r.error());
  while (true) {
    auto line = child_.read_line(60s);
    if (!line) return std::unexpected(line.error());
    if (!*line) return fail("mcp " + name_ + ": timed out waiting for " + method);
    log::debug(log::Cat::mcp, "{} <- {}", name_, **line);
    Json reply;
    try {
      reply = Json::parse(**line);
    } catch (...) {
      continue;  // not JSON (stray log output); ignore
    }
    if (!reply.contains("id") || reply["id"] != id) continue;  // notification or other reply
    if (reply.contains("error")) return fail("mcp " + name_ + ": " + reply["error"].value("message", "error"));
    return reply.value("result", Json::object());
  }
}

Result<std::vector<Json>> Client::list_tools() {
  auto r = call("tools/list", Json::object());
  if (!r) return std::unexpected(r.error());
  return r->value("tools", std::vector<Json>{});
}

namespace {

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

}  // namespace

std::vector<std::shared_ptr<Client>> load(const Config& config, tool::Registry& tools) {
  std::vector<std::shared_ptr<Client>> clients;
  for (auto& [name, cfg] : config.mcp) {
    if (!cfg.enabled || cfg.command.empty()) continue;
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
  return clients;
}

}  // namespace shaman::mcp
