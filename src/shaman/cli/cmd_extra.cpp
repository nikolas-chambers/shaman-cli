// worktree, schedule, doctor, completion
#include <format>
#include <iostream>

#include "shaman/cli/commands.hpp"
#include "shaman/cli/render.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/extras/schedule.hpp"
#include "shaman/extras/worktree.hpp"
#include "shaman/http/http.hpp"
#include "shaman/lsp/lsp.hpp"
#include "shaman/skill/skill.hpp"
#include "shaman/tui/app.hpp"
#include "shaman/tui/markdown.hpp"
#include "shaman/version.hpp"

namespace shaman::cli {
namespace fs = std::filesystem;

int cmd_worktree(App& app, const Options& o, const Args& args) {
  auto& pos = args.positional;
  auto sub = pos.size() > 1 ? pos[1] : "list";
  if (sub == "list") {
    auto r = extras::worktree_list(app.root);
    return r ? (std::cout << *r << "\n", 0) : (std::cerr << r.error().message << "\n", 1);
  }
  if (sub == "remove" && pos.size() > 2) {
    auto r = extras::worktree_remove(app.root, pos[2]);
    return r ? (std::cout << "removed worktree " << pos[2] << "\n", 0) : (std::cerr << r.error().message << "\n", 1);
  }
  auto dir = extras::worktree_create(app.root, sub);
  if (!dir) return std::cerr << "worktree: " << dir.error().message << "\n", 1;
  std::cerr << "worktree: " << dir->string() << " (branch shaman/" << sub << ")\n";
  auto wt = App::create(*dir, true);
  if (!wt) return std::cerr << wt.error().message << "\n", 1;
  std::error_code ec;
  fs::current_path(*dir, ec);
  std::optional<std::string> first;
  if (pos.size() > 2) first = str::join({pos.begin() + 2, pos.end()}, " ");
  if (!stdin_is_tty() || o.plain) return first ? cmd_run(**wt, o, *first) : cmd_repl(**wt, o);
  return tui::run(**wt, {o.model, o.agent, std::nullopt, first, false, o.yolo});
}

int cmd_schedule(App& app, const Options& o, const Args& args) {
  auto& pos = args.positional;
  auto sub = pos.size() > 1 ? pos[1] : "list";
  if (sub == "list") {
    auto r = extras::schedule_list();
    return r ? (std::cout << *r << "\n", 0) : (std::cerr << r.error().message << "\n", 1);
  }
  if (sub == "remove" && pos.size() > 2) {
    auto r = extras::schedule_remove(pos[2]);
    return r ? (std::cout << "removed " << pos[2] << "\n", 0) : (std::cerr << r.error().message << "\n", 1);
  }
  if (sub == "add" && pos.size() > 3) {
    std::string flags;
    if (o.model) flags += "-m " + *o.model + " ";
    if (o.agent) flags += "-a " + *o.agent + " ";
    if (o.yolo) flags += "--yolo ";
    auto r = extras::schedule_add(app.root, pos[2], str::join({pos.begin() + 3, pos.end()}, " "), flags);
    if (!r) return std::cerr << r.error().message << "\n", 1;
    std::cout << "scheduled " << *r << ": runs \"" << pos[2] << "\" in " << app.root.string()
              << "\nlogs: " << (paths::data_dir() / "schedule" / (*r + ".log")).string() << "\n";
    return 0;
  }
  return std::cerr << "usage: shaman schedule add \"<cron>\" <prompt> | list | remove <id>\n", 2;
}

int cmd_doctor(App& app, const Args&) {
  int problems = 0;
  bool u = tui::unicode(), color = stdout_is_tty();
  auto paint = [&](const char* code, const std::string& s) { return color ? std::string(code) + s + "\x1b[0m" : s; };
  auto line = [&](bool ok, const std::string& what, const std::string& detail, bool optional = false) {
    if (!ok && !optional) ++problems;
    std::string mark = ok ? paint("\x1b[32m", u ? "✓" : "ok  ") : optional ? paint("\x1b[33m", u ? "•" : "--  ") : paint("\x1b[31m", u ? "✗" : "FAIL");
    std::cout << "  " << mark << " " << what << (detail.empty() ? "" : "  " + paint("\x1b[2m", detail)) << "\n";
  };
  std::cout << "shaman " << kVersion << "\n\nsetup\n";
  line(true, "config", app.config.sources.empty() ? "defaults (no shaman.json)" : std::to_string(app.config.sources.size()) + " file(s)");
  std::error_code ec;
  fs::create_directories(paths::data_dir(), ec);
  line(!ec, "data dir writable", paths::data_dir().string());
  auto d = app.providers->default_model();
  line(bool(d), "default model", d ? d->ref() : d.error().message);

  std::cout << "\nproviders\n";
  for (auto& p : app.providers->providers()) {
    auto k = app.providers->key(p);
    if (k.source.empty()) {
      line(false, p.name, "no key (set " + (p.env.empty() ? std::string("one") : p.env.front()) + " or `shaman auth login " + p.id + "`)", true);
      continue;
    }
    http::Request req;
    req.url = p.base_url + "/models";
    req.timeout_s = 6;
    if (!k.key.empty()) req.headers = {{"Authorization", "Bearer " + k.key}, {"x-api-key", k.key}, {"anthropic-version", "2023-06-01"}};
    auto res = http::send(req);
    bool reach = res && res->status > 0 && res->status < 500;
    bool auth = res && res->status != 401 && res->status != 403;
    line(reach && auth, p.name,
         k.source + (!res ? "; unreachable: " + res.error().message
                          : res->status == 401 || res->status == 403 ? "; key rejected (HTTP " + std::to_string(res->status) + ")"
                                                                     : "; reachable"),
         p.keyless && !reach);
  }

  std::cout << "\ntools\n";
  for (auto [bin, why, optional] : std::initializer_list<std::tuple<const char*, const char*, bool>>{
           {"git", "undo snapshots, worktrees, GitHub", false}, {"rg", "fast grep/glob (falls back to built-in search)", true},
           {"python3", "skill helper scripts", true}, {"soffice", "xlsx recalc, office to PDF", true}, {"gh", "shaman pr", true}}) {
    auto p = process::which(bin);
    line(bool(p), bin, p ? p->string() : std::string("not found: ") + why, optional);
  }
  for (auto& s : app.lsp->servers()) {
    auto p = process::which(s.command.front());
    if (p) line(true, "lsp " + s.id, p->string());
  }
  std::cout << "\nextensions\n";
  line(true, "skills", std::to_string(skill::discover(app.root).size()) + " available");
  line(true, "MCP servers", std::to_string(app.config.mcp.size()) + " configured");
  std::cout << "\nterminal\n";
  line(u, "UTF-8 locale", u ? "" : "set LANG=C.UTF-8 (or similar) for nicer symbols; ASCII fallback in use", true);
  std::cout << "\n" << (problems ? std::to_string(problems) + " problem(s) found" : std::string("all good")) << "\n";
  return problems ? 1 : 0;
}

int cmd_completion(const Args& args) {
  auto shell = args.positional.size() > 1 ? args.positional[1] : "bash";
  const char* commands =
      "run serve web acp models auth agents agent sessions export import share stats mcp plugins pr github upgrade debug "
      "worktree schedule doctor completion version help";
  if (shell == "bash") {
    std::cout << "# shaman bash completion: eval \"$(shaman completion bash)\"\n"
                 "_shaman() {\n  local cur=${COMP_WORDS[COMP_CWORD]}\n  if [ $COMP_CWORD -eq 1 ]; then\n"
                 "    COMPREPLY=($(compgen -W \"" << commands << "\" -- \"$cur\"))\n"
                 "  elif [[ $cur == -* ]]; then\n"
                 "    COMPREPLY=($(compgen -W \"--model --agent --continue --session --file --command --attach --goal --format --yolo "
                 "--reasoning --plain --no-mcp --debug --trace --help\" -- \"$cur\"))\n"
                 "  else COMPREPLY=($(compgen -f -- \"$cur\")); fi\n}\ncomplete -F _shaman shaman\n";
  } else if (shell == "zsh") {
    std::cout << "# shaman zsh completion: eval \"$(shaman completion zsh)\"\n#compdef shaman\n"
                 "_shaman() {\n  if (( CURRENT == 2 )); then\n    compadd -- " << commands << "\n"
                 "  else\n    _arguments '--model[model]:model:' '--agent[agent]:agent:' '--continue' '--session[session]:id:' "
                 "'--file[attach]:file:_files' '--goal[goal]:text:' '--yolo' '--plain' '--debug' '*:file:_files'\n  fi\n}\n"
                 "compdef _shaman shaman\n";
  } else if (shell == "fish") {
    std::cout << "# shaman fish completion: shaman completion fish | source\n";
    for (auto& c : str::split(commands, ' ')) std::cout << "complete -c shaman -n '__fish_use_subcommand' -a " << c << "\n";
    for (auto f : {"model", "agent", "continue", "session", "file", "command", "attach", "goal", "format", "yolo", "plain", "debug", "trace"})
      std::cout << "complete -c shaman -l " << f << "\n";
  } else {
    return std::cerr << "usage: shaman completion bash|zsh|fish\n", 2;
  }
  return 0;
}

}  // namespace shaman::cli
