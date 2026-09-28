// `shaman debug <what>`: inspect how shaman is wired without running a model.
#include <format>
#include <iostream>

#include "shaman/cli/commands.hpp"
#include "shaman/command/command.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/session/system_prompt.hpp"
#include "shaman/skill/skill.hpp"

namespace shaman::cli {

int cmd_debug(App& app, const Args& args) {
  auto& pos = args.positional;
  auto what = pos.size() > 1 ? pos[1] : "";
  if (what == "paths") {
    std::cout << "config   " << paths::config_dir().string() << "\ndata     " << paths::data_dir().string()
              << "\ncache    " << paths::cache_dir().string() << "\nproject  " << app.root.string()
              << "\nsessions " << app.store->dir().string() << "\nauth     " << auth::Store::default_path().string() << "\n";
  } else if (what == "config") {
    std::cout << "sources:\n";
    for (auto& s : app.config.sources) std::cout << "  " << s.string() << "\n";
    std::cout << "merged:\n" << app.config.raw.dump(2) << "\n";
  } else if (what == "models") {
    print_models(app, true);
    auto d = app.providers->default_model();
    std::cout << "default: " << (d ? d->ref() + " via " + d->endpoint.base_url : d.error().message) << "\nfree fallback chain:";
    for (auto& f : app.providers->free_fallbacks(d ? d->ref() : "")) std::cout << " " << f;
    std::cout << "\n";
  } else if (what == "agents") {
    for (auto* a : app.agents->list()) {
      Json tools = a->tools;
      std::cout << std::format("{} ({}, from {})\n  tools: {}\n  permission: {}\n", a->name,
                               a->mode == agent::Mode::subagent ? "subagent" : "primary", a->source, tools.dump(), a->permission.dump());
    }
  } else if (what == "tools") {
    for (auto* t : app.tools.all()) std::cout << t->name() << "\n  " << t->description() << "\n  " << t->schema().dump() << "\n";
  } else if (what == "prompt") {
    auto* a = app.agents->find(pos.size() > 2 ? pos[2] : app.config.default_agent);
    if (!a) return std::cerr << "unknown agent\n", 1;
    auto m = app.providers->default_model();
    std::cout << session::system_prompt(*a, app.config, app.root, m ? m->ref() : "?") << "\n";
  } else if (what == "permission") {
    auto rules = permission::Rules::defaults();
    rules.push(permission::Rules::from_json(app.config.permission));
    if (pos.size() > 3) std::cout << permission::to_string(rules.evaluate(pos[2], pos[3])) << "\n";
    else std::cout << rules.to_json().dump(2) << "\n(check one: shaman debug permission bash \"git push\")\n";
  } else if (what == "session") {
    auto s = pos.size() > 2 ? app.store->get(pos[2])
             : app.store->latest() ? Result<session::Info>(*app.store->latest()) : fail("no sessions");
    if (!s) return std::cerr << s.error().message << "\n", 1;
    std::cout << session::to_json(*s).dump(2) << "\n";
    if (auto ms = app.store->messages(s->id))
      for (auto& m : *ms) std::cout << llm::to_json(m).dump() << "\n";
  } else if (what == "lsp") {
    for (auto& s : app.lsp->servers())
      std::cout << std::format("  {:<14} {:<40} {}\n", s.id, s.command.front() + (process::which(s.command.front()) ? "" : " (not installed)"),
                               [&] { std::string e; for (auto& x : s.extensions) e += x + " "; return e; }());
    if (pos.size() > 2) {  // shaman debug lsp path/to/file
      auto p = paths::resolve(app.root, pos[2]);
      auto r = app.lsp->query("diagnostics", p, 1, 1);
      std::cout << (r ? *r : r.error().message) << "\n";
    }
  } else if (what == "skills") {
    for (auto& s : skill::discover(app.root))
      std::cout << "  " << s.name << "  (" << s.source << ")\n    " << s.description << "\n";
  } else if (what == "commands") {
    for (auto& c : command::discover(app.config, app.root)) std::cout << "  /" << c.name << "  (" << c.source << ")  " << c.description << "\n";
  } else if (what == "plugins") {
    for (auto& p : app.plugins.plugins()) std::cout << "  " << p->name() << "\n";
    if (app.plugins.empty()) std::cout << "no plugins loaded\n";
  } else {
    std::cerr << "shaman debug <config|paths|models|agents|tools|prompt [agent]|permission [perm subject]|session [id]|"
                 "lsp [file]|skills|commands|plugins>\n";
    return 2;
  }
  return 0;
}

}  // namespace shaman::cli
