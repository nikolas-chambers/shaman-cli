// models, auth, agents, sessions, export, import, share, stats, mcp, plugins, pr
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

#include "shaman/cli/commands.hpp"
#include "shaman/cli/render.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"
#include "shaman/mcp/mcp.hpp"
#include "shaman/mcp/oauth.hpp"
#include "shaman/provider/catalog.hpp"
#include "shaman/session/archive.hpp"
#include "shaman/tui/app.hpp"

namespace shaman::cli {
namespace fs = std::filesystem;

void print_models(App& app, bool all) {
  for (auto& p : app.providers->providers()) {
    bool usable = app.providers->usable(p);
    if (!usable && !all) continue;
    auto key = app.providers->key(p);
    auto models = app.providers->visible_models(p);
    std::cout << std::format("{} {}\n", p.name,
                             usable ? "(" + key.source + ")" : "(needs " + (p.env.empty() ? "a key" : p.env.front()) + ")");
    for (auto& m : models)
      std::cout << std::format("  {:<44} {}{}\n", p.id + "/" + m.id, m.name,
                               m.free() ? "  free" : m.cost_input > 0 ? std::format("  ${}/${} per 1M", m.cost_input, m.cost_output) : "");
    if (models.empty()) std::cout << "  (any model id; `shaman models --refresh` lists them)\n";
  }
}

int cmd_models(App& app, const Args& args) {
  if (args.has("refresh")) {
    for (auto& p : app.providers->providers()) {
      if (!app.providers->usable(p)) continue;
      auto r = provider::refresh(p, app.providers->key(p).key);
      std::cerr << (r ? std::format("{}: {} models\n", p.id, r->size()) : p.id + ": " + r.error().message + "\n");
    }
    std::error_code ec;
    auto fresh = App::create(std::filesystem::current_path(ec), false);
    if (fresh) print_models(**fresh, args.has("all"));
    return 0;
  }
  print_models(app, args.has("all"));
  return 0;
}

int cmd_auth(App& app, const Args& args) {
  auto& pos = args.positional;
  auto sub = pos.size() > 1 ? pos[1] : "list";
  if (sub == "list" || sub == "ls") {
    for (auto& p : app.providers->providers()) {
      auto k = app.providers->key(p);
      std::cout << std::format("  {:<12} {}\n", p.id, k.source.empty() ? "-" : k.source);
    }
    return 0;
  }
  if (sub == "login") {
    std::string id = pos.size() > 2 ? pos[2] : "";
    if (id.empty()) {
      std::cout << "Provider (";
      for (auto& p : app.providers->providers())
        if (!p.keyless) std::cout << p.id << " ";
      std::cout << "): ";
      std::getline(std::cin, id);
      id = str::trim(id);
    }
    auto* p = app.providers->find(id);
    if (!p) return std::cerr << "unknown provider: " << id << "\n", 1;
    if (!p->key_url.empty()) std::cout << "Get a key at " << p->key_url << "\n";
    std::cout << "API key for " << p->name << ": " << std::flush;
    std::string key;
    std::getline(std::cin, key);
    key = str::trim(key);
    if (key.empty()) return std::cerr << "no key entered\n", 1;
    if (auto r = app.auth->set(id, key); !r) return std::cerr << r.error().message << "\n", 1;
    std::cout << "Saved. `shaman models` now lists " << p->name << " models.\n";
    return 0;
  }
  if (sub == "logout") {
    if (pos.size() < 3) return std::cerr << "usage: shaman auth logout <provider>\n", 2;
    auto r = app.auth->remove(pos[2]);
    return r ? 0 : (std::cerr << r.error().message << "\n", 1);
  }
  return std::cerr << "usage: shaman auth login|list|logout [provider]\n", 2;
}

int cmd_agents(App& app, const Args& args) {
  auto& pos = args.positional;
  if (pos.size() > 2 && pos[1] == "create") {
    auto path = app.root / ".shaman" / "agents" / (pos[2] + ".md");
    std::error_code ec;
    if (fs::exists(path, ec)) return std::cerr << path.string() << " exists\n", 1;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path) << "---\ndescription: What this agent is for\nmode: primary\n# model: opencode/big-pickle\n"
                           "# tools: read, grep, glob, list, bash\n---\nYou are ... Describe how the agent should work.\n";
    std::cout << "Created " << path.lexically_relative(app.root).string() << "\n";
    return 0;
  }
  for (auto* a : app.agents->list())
    std::cout << std::format("  {:<10} {:<9} {}\n", a->name, a->mode == agent::Mode::subagent ? "subagent" : "primary", a->description);
  return 0;
}

int cmd_sessions(App& app, const Args&) {
  for (auto& s : app.store->list())
    std::cout << std::format("  {}  {:<8} {:>8} tok  {}\n", s.id, s.agent, s.usage.total(), s.title);
  return 0;
}

static Result<session::Info> pick(App& app, const Args& args) {
  if (args.positional.size() > 1) return app.store->get(args.positional[1]);
  if (auto l = app.store->latest()) return *l;
  return fail("no sessions");
}

int cmd_export(App& app, const Args& args) {
  auto s = pick(app, args);
  if (!s) return std::cerr << s.error().message << "\n", 1;
  auto ms = app.store->messages(s->id);
  if (!ms) return std::cerr << ms.error().message << "\n", 1;
  auto fmt = args.get("format").value_or("md");
  std::string out = fmt == "json" ? session::export_json(*s, *ms).dump(2)
                  : fmt == "html" ? session::export_html(*s, *ms)
                                  : session::export_markdown(*s, *ms);
  if (auto path = args.get("output")) {
    std::ofstream(*path) << out;
    std::cerr << "wrote " << *path << "\n";
  } else {
    std::cout << out;
  }
  return 0;
}

int cmd_import(App& app, const Args& args) {
  if (args.positional.size() < 2) return std::cerr << "usage: shaman import <file.json>\n", 2;
  std::ifstream in(args.positional[1]);
  if (!in) return std::cerr << "cannot read " << args.positional[1] << "\n", 1;
  try {
    auto r = session::import_json(*app.store, Json::parse(in));
    if (!r) return std::cerr << r.error().message << "\n", 1;
    std::cout << "imported as " << r->id << " (continue with: shaman -s " << r->id << ")\n";
    return 0;
  } catch (const std::exception& e) {
    return std::cerr << "invalid file: " << e.what() << "\n", 1;
  }
}

// Writes a self-contained HTML page. With config "share": {"url": ..., "token": ...}
// the page is also POSTed there and the returned {"url"} printed.
int cmd_share(App& app, const Args& args) {
  auto s = pick(app, args);
  if (!s) return std::cerr << s.error().message << "\n", 1;
  auto ms = app.store->messages(s->id);
  if (!ms) return std::cerr << ms.error().message << "\n", 1;
  auto html = session::export_html(*s, *ms);
  auto dir = paths::data_dir() / "shares";
  std::error_code ec;
  fs::create_directories(dir, ec);
  auto path = dir / (s->id + ".html");
  std::ofstream(path) << html;
  std::cout << "page: " << path.string() << "\n";
  auto share = app.config.raw.value("share", Json::object());
  if (share.is_object() && share.contains("url")) {
    http::Request req{"POST", share["url"], {{"Content-Type", "text/html"}}, html};
    if (share.contains("token")) req.headers.emplace_back("Authorization", "Bearer " + share["token"].get<std::string>());
    auto res = http::send(req);
    if (!res || res->status >= 300) return std::cerr << "upload failed\n", 1;
    try {
      std::cout << "url:  " << Json::parse(res->body).value("url", res->body) << "\n";
    } catch (...) {
      std::cout << "url:  " << res->body << "\n";
    }
  }
  return 0;
}

int cmd_stats(App& app, const Args& args) {
  int days = std::stoi(args.get("days").value_or("0"));
  auto st = session::compute_stats(*app.store, days);
  std::cout << std::format("{}\n  sessions {}   turns {}   messages {}\n  tokens {} in / {} out ({} cached)   cost ${:.4f}\n"
                           "  tool calls {} ({} errors)\n",
                           days ? std::format("Last {} days", days) : "All time", st.sessions, st.turns, st.messages,
                           st.input_tokens, st.output_tokens, st.cache_read, st.cost, st.tool_calls, st.tool_errors);
  auto top = [](const char* title, const std::map<std::string, int>& m) {
    std::vector<std::pair<int, std::string>> v;
    for (auto& [k, n] : m) v.emplace_back(n, k);
    std::ranges::sort(v, std::greater{});
    if (v.empty()) return;
    std::cout << "  " << title << ":";
    for (size_t i = 0; i < v.size() && i < 8; ++i) std::cout << " " << v[i].second << " (" << v[i].first << ")";
    std::cout << "\n";
  };
  top("tools", st.tools);
  top("models", st.models);
  top("agents", st.agents);
  return 0;
}

int cmd_mcp(App& app, const Args& args) {
  auto& pos = args.positional;
  auto sub = pos.size() > 1 ? pos[1] : "list";
  if (sub == "serve") return mcp::serve(app.config, app.root, app.tools, args.has("yolo"));
  if (sub == "list" || sub == "ls") {
    if (app.config.mcp.empty()) std::cout << "no MCP servers configured (see README: Config)\n";
    for (auto& [name, cfg] : app.config.mcp) {
      std::string where = cfg.type == "remote" ? cfg.url : str::join(cfg.command, " ");
      std::string state = !cfg.enabled ? "disabled" : cfg.type == "remote" && mcp::oauth::has_tokens(name) ? "logged in" : "";
      auto client = cfg.enabled ? mcp::Client::connect(name, cfg) : Result<std::shared_ptr<mcp::Client>>(fail("disabled"));
      std::string status = client ? "connected" : client.error().message;
      if (client)
        if (auto tools = (*client)->list_tools()) status += std::format(", {} tools", tools->size());
      std::cout << std::format("  {:<14} {:<7} {}\n    {}{}\n", name, cfg.type, where, status, state.empty() ? "" : " · " + state);
    }
    return 0;
  }
  if ((sub == "auth" || sub == "logout") && pos.size() > 2) {
    auto it = app.config.mcp.find(pos[2]);
    if (it == app.config.mcp.end()) return std::cerr << "no MCP server named " << pos[2] << "\n", 1;
    auto r = sub == "auth" ? mcp::oauth::login(pos[2], it->second) : mcp::oauth::logout(pos[2]);
    if (!r) return std::cerr << r.error().message << "\n", 1;
    std::cout << (sub == "auth" ? "Logged in to " : "Logged out of ") << pos[2] << "\n";
    return 0;
  }
  return std::cerr << "usage: shaman mcp list|auth <name>|logout <name>|serve\n", 2;
}

int cmd_plugins(App& app, const Args&) {
  if (app.plugins.empty()) std::cout << "no plugins loaded (see docs/PLUGINS.md)\n";
  for (auto& p : app.plugins.plugins()) std::cout << "  " << p->name() << "\n";
  return 0;
}

int cmd_pr(App& app, const Options& o, const Args& args) {
  if (args.positional.size() < 2) return std::cerr << "usage: shaman pr <number>\n", 2;
  auto n = args.positional[1];
  if (!process::which("gh")) return std::cerr << "shaman pr needs the GitHub CLI (gh)\n", 1;
  auto co = process::shell("gh pr checkout " + n, {.cwd = app.root, .timeout = std::chrono::minutes(2)});
  if (!co || co->exit_code != 0) return std::cerr << "gh pr checkout failed:\n" << (co ? co->output : co.error().message) << "\n", 1;
  auto view = process::shell("gh pr view " + n + " --json title,body,url -q '.title + \"\\n\" + .url + \"\\n\\n\" + .body'",
                             {.cwd = app.root, .timeout = std::chrono::seconds(30)});
  std::string prompt = std::format("I checked out pull request #{}.\n\n{}\nStart by summarising what it changes (git diff against its base) "
                                   "and wait for my instructions.",
                                   n, view ? view->output : "");
  if (!stdin_is_tty() || o.plain) return cmd_run(app, o, prompt);
  return tui::run(app, {o.model, o.agent, std::nullopt, prompt, false, o.yolo});
}

}  // namespace shaman::cli
