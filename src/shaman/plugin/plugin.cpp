#include "shaman/plugin/plugin.hpp"

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/version.hpp"

namespace shaman::plugin {
using namespace std::chrono_literals;

Result<std::shared_ptr<ProcessPlugin>> ProcessPlugin::start(const std::vector<std::string>& command, const fs::path& root) {
  auto child = process::Child::spawn(command, {{"SHAMAN_PROJECT", root.string()}});
  if (!child) return std::unexpected(child.error());
  auto p = std::shared_ptr<ProcessPlugin>(new ProcessPlugin(std::move(*child)));
  auto init = p->call("initialize", {{"version", kVersion}, {"protocol", 1}, {"root", root.string()}}, 10s);
  if (!init) return std::unexpected(init.error());
  p->name_ = init->value("name", command.back());
  p->hooks_ = init->value("hooks", std::vector<std::string>{});
  p->tools_ = init->value("tools", std::vector<Json>{});
  log::debug(log::Cat::agent, "plugin {}: hooks {} tools {}", p->name_, Json(p->hooks_).dump(), p->tools_.size());
  return p;
}

Result<Json> ProcessPlugin::call(const std::string& method, const Json& params, std::chrono::milliseconds timeout) {
  std::lock_guard lock(mu_);
  int id = next_id_++;
  Json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
  log::debug(log::Cat::agent, "plugin {} -> {}", name_, msg.dump().substr(0, 300));
  if (auto r = child_.write_line(msg.dump()); !r) return std::unexpected(r.error());
  while (true) {
    auto line = child_.read_line(timeout);
    if (!line) return std::unexpected(line.error());
    if (!*line) return fail("plugin " + name_ + ": timed out on " + method);
    Json reply;
    try {
      reply = Json::parse(**line);
    } catch (...) {
      continue;  // plugins may print junk; ignore non-JSON lines
    }
    log::debug(log::Cat::agent, "plugin {} <- {}", name_, reply.dump().substr(0, 300));
    if (reply.value("id", -1) != id) continue;
    if (reply.contains("error")) return fail("plugin " + name_ + ": " + reply["error"].value("message", "error"));
    return reply.value("result", Json::object());
  }
}

bool ProcessPlugin::wants(const std::string& hook) const {
  return std::ranges::find(hooks_, hook) != hooks_.end() || std::ranges::find(hooks_, "*") != hooks_.end();
}

void ProcessPlugin::tool_before(ToolBefore& ev) {
  if (!wants("tool.before")) return;
  auto r = call("tool.before", {{"session", ev.session}, {"tool", ev.tool}, {"input", ev.input}}, 10s);
  if (!r) return log::warn(r.error().message);
  if (r->contains("input") && (*r)["input"].is_object()) ev.input = (*r)["input"];
  if (auto b = r->find("block"); b != r->end() && b->is_string()) ev.block = b->get<std::string>();
}

void ProcessPlugin::tool_after(ToolAfter& ev) {
  if (!wants("tool.after")) return;
  auto r = call("tool.after", {{"session", ev.session}, {"tool", ev.tool}, {"input", ev.input},
                               {"output", ev.output}, {"is_error", ev.is_error}}, 10s);
  if (!r) return log::warn(r.error().message);
  if (auto o = r->find("output"); o != r->end() && o->is_string()) ev.output = o->get<std::string>();
}

std::optional<permission::Action> ProcessPlugin::permission_ask(const permission::Request& req) {
  if (!wants("permission.ask")) return std::nullopt;
  auto r = call("permission.ask", {{"permission", req.permission}, {"subject", req.subject}, {"title", req.title}}, 10s);
  if (!r) return log::warn(r.error().message), std::nullopt;
  auto a = r->value("action", "");
  if (a == "allow") return permission::Action::allow;
  if (a == "deny") return permission::Action::deny;
  return std::nullopt;
}

void ProcessPlugin::chat_system(const std::string& agent, std::string& system) {
  if (!wants("chat.system")) return;
  auto r = call("chat.system", {{"agent", agent}, {"system", system}}, 10s);
  if (r && r->contains("system")) system = (*r)["system"].get<std::string>();
}

void ProcessPlugin::chat_message(const std::string& session, std::string& text) {
  if (!wants("chat.message")) return;
  auto r = call("chat.message", {{"session", session}, {"text", text}}, 10s);
  if (r && r->contains("text")) text = (*r)["text"].get<std::string>();
}

void ProcessPlugin::event(const std::string& type, const Json& data) {
  if (!wants("event") && !wants(type)) return;
  std::lock_guard lock(mu_);
  child_.write_line(Json{{"jsonrpc", "2.0"}, {"method", "event"}, {"params", {{"type", type}, {"data", data}}}}.dump());
}

namespace {

class PluginTool final : public tool::Tool {
 public:
  PluginTool(std::shared_ptr<ProcessPlugin> p, Json spec) : plugin_(std::move(p)), spec_(std::move(spec)) {}
  std::string name() const override { return spec_.value("name", ""); }
  std::string description() const override { return spec_.value("description", ""); }
  Json schema() const override { return spec_.value("parameters", Json{{"type", "object"}, {"properties", Json::object()}}); }
  tool::Output run(const Json& input, tool::Context& ctx) override {
    if (!ctx.permit(spec_.value("permission", "plugin"), name(), plugin_->name() + ": " + name()))
      return tool::error("permission denied");
    auto r = plugin_->call("tool.call", {{"name", name()}, {"input", input}, {"session", ctx.session_id},
                                         {"root", ctx.root.string()}}, std::chrono::minutes(10));
    if (!r) return tool::error(r.error().message);
    return {tool::truncate(r->value("output", "")), r->value("is_error", false), r->value("title", name())};
  }

 private:
  std::shared_ptr<ProcessPlugin> plugin_;
  Json spec_;
};

}  // namespace

void Host::add(std::shared_ptr<Hooks> hooks) { hooks_.push_back(std::move(hooks)); }

void Host::load(const Config& config, const fs::path& root, tool::Registry& tools) {
  std::vector<std::vector<std::string>> commands;
  for (auto& entry : config.raw.value("plugin", Json::array())) {
    if (entry.is_string()) commands.push_back({paths::resolve(root, entry.get<std::string>()).string()});
    else if (entry.is_object() && entry.contains("command")) commands.push_back(entry["command"].get<std::vector<std::string>>());
  }
  for (auto dir : {paths::config_dir() / "plugins", root / ".shaman" / "plugins"}) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
      auto perms = e.status(ec).permissions();
      bool exec = (perms & fs::perms::owner_exec) != fs::perms::none;
      if (e.is_regular_file(ec) && exec) commands.push_back({e.path().string()});
    }
  }
  for (auto& cmd : commands) {
    auto p = ProcessPlugin::start(cmd, root);
    if (!p) {
      log::warn("plugin " + cmd.front() + ": " + p.error().message);
      continue;
    }
    for (auto& spec : (*p)->tools()) tools.add(std::make_unique<PluginTool>(*p, spec));
    add(*p);
  }
}

void Host::tool_before(ToolBefore& ev) {
  for (auto& h : hooks_) {
    h->tool_before(ev);
    if (ev.block) return;
  }
}
void Host::tool_after(ToolAfter& ev) {
  for (auto& h : hooks_) h->tool_after(ev);
}
std::optional<permission::Action> Host::permission_ask(const permission::Request& req) {
  for (auto& h : hooks_)
    if (auto a = h->permission_ask(req)) return a;
  return std::nullopt;
}
void Host::chat_system(const std::string& agent, std::string& system) {
  for (auto& h : hooks_) h->chat_system(agent, system);
}
void Host::chat_message(const std::string& session, std::string& text) {
  for (auto& h : hooks_) h->chat_message(session, text);
}
void Host::event(const std::string& type, const Json& data) {
  for (auto& h : hooks_) h->event(type, data);
}

}  // namespace shaman::plugin
