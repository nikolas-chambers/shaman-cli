#include "shaman/cli/main.hpp"

#include <csignal>
#include <cstdlib>
#include <format>
#include <iostream>
#include <iterator>

#include "shaman/cli/app.hpp"
#include "shaman/cli/args.hpp"
#include "shaman/cli/render.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/provider/catalog.hpp"
#include "shaman/session/system_prompt.hpp"
#include "shaman/version.hpp"

namespace shaman::cli {
namespace {

std::atomic<bool> g_cancel{false};
std::atomic<bool> g_busy{false};

void on_sigint(int) {
  if (g_busy.load()) g_cancel = true;  // first ^C stops the turn
  else std::_Exit(130);                 // idle: quit
}

constexpr const char* kHelp = R"(shaman - a coding agent for your terminal

usage:
  shaman [options]                 interactive session
  shaman run [options] <message>   one-shot; also reads piped stdin
  shaman models [--refresh]        list models (refresh pulls the live free list)
  shaman agents                    list agents
  shaman sessions                  list sessions for this project
  shaman debug <what>              inspect internals: config paths models agents
                                   tools prompt permission session
  shaman version

options:
  -m, --model <provider/model>     model to use (default: best free model)
  -a, --agent <name>               agent to use (default: build)
  -c, --continue                   continue the most recent session
  -s, --session <id>               continue a specific session
      --format <text|json>         run output format
      --yolo                       allow every tool call without asking
      --reasoning                  show model reasoning when available
      --no-mcp                     skip MCP servers
      --debug[=cats]               debug log: all, or config,provider,http,sse,
                                   tool,permission,session,agent,mcp
      --log-file <path>            write debug log to a file instead of stderr
      --trace <path>               record every request/event/tool call as JSONL

environment:
  SHAMAN_DEBUG, SHAMAN_MODEL, SHAMAN_CONFIG, SHAMAN_CONFIG_CONTENT, OPENCODE_API_KEY
)";

struct Options {
  std::optional<std::string> model, agent, session;
  bool cont = false, yolo = false, reasoning = false, json = false;
};

Result<session::Info> open_session(App& app, const Options& o) {
  if (o.session) return app.store->get(*o.session);
  if (o.cont)
    if (auto latest = app.store->latest()) return *latest;
  auto agent = o.agent.value_or(app.config.default_agent);
  auto* a = app.agents->find(agent);
  if (!a || a->mode == agent::Mode::subagent) return fail("unknown primary agent: " + agent);
  return app.store->create(agent, o.model.value_or(""));
}

int run_once(App& app, const Options& o, std::string message) {
  if (!stdin_is_tty()) {
    std::string piped(std::istreambuf_iterator<char>(std::cin), {});
    if (!piped.empty()) message = message.empty() ? piped : message + "\n\n" + piped;
  }
  if (str::trim(message).empty()) {
    std::cerr << "shaman run: no message\n";
    return 2;
  }
  auto s = open_session(app, o);
  if (!s) return std::cerr << "error: " << s.error().message << "\n", 1;
  if (o.agent) s->agent = *o.agent;
  session::Runner runner(app.services(ask_terminal, &g_cancel, o.yolo));
  TerminalEvents term(o.reasoning);
  JsonEvents json;
  session::Events& ev = o.json ? static_cast<session::Events&>(json) : term;
  g_busy = true;
  auto r = runner.prompt(*s, message, ev, o.model);
  g_busy = false;
  term.finish();
  if (!r) return std::cerr << "error: " << r.error().message << "\n", 1;
  return 0;
}

void print_models(App& app) {
  for (auto& p : app.providers->providers()) {
    std::cout << p.name << "\n";
    for (auto& m : p.models)
      std::cout << std::format("  {}/{:<32} {}{}\n", p.id, m.id, m.name, m.free() ? "  (free)" : "");
  }
}

int repl(App& app, Options o) {
  auto s = open_session(app, o);
  if (!s) return std::cerr << "error: " << s.error().message << "\n", 1;
  session::Runner runner(app.services(ask_terminal, &g_cancel, o.yolo));
  bool color = stdout_is_tty();
  auto model = app.providers->resolve(o.model.value_or(s->model));
  if (!model) model = app.providers->default_model();
  std::cout << std::format("shaman {} | agent {} | model {} | session {}\n/help for commands, ^C stops a turn, ^D quits\n",
                           kVersion, s->agent, model ? model->ref() : "?", s->id);

  std::string line;
  while (true) {
    std::cout << (color ? "\x1b[1m> \x1b[0m" : "> ") << std::flush;
    if (!std::getline(std::cin, line)) break;
    line = str::trim(line);
    if (line.empty()) continue;
    if (line.starts_with("/")) {
      auto parts = str::split(line, ' ');
      auto cmd = parts[0];
      auto arg = parts.size() > 1 ? str::trim(line.substr(cmd.size())) : std::string();
      if (cmd == "/exit" || cmd == "/quit") break;
      else if (cmd == "/help")
        std::cout << "/new  /sessions  /agent <name>  /model <ref>  /models  /undo  /compact  /todos  /cost  /exit\n";
      else if (cmd == "/new") {
        auto n = app.store->create(s->agent, s->model);
        if (n) s = n, std::cout << "new session " << s->id << "\n";
      } else if (cmd == "/sessions")
        for (auto& i : app.store->list()) std::cout << "  " << i.id << "  " << i.title << "\n";
      else if (cmd == "/agent") {
        auto* a = app.agents->find(arg);
        if (a && a->mode != agent::Mode::subagent) s->agent = arg, std::cout << "agent: " << arg << "\n";
        else std::cout << "unknown primary agent\n";
      } else if (cmd == "/model") {
        auto m = app.providers->resolve(arg);
        if (m) o.model = m->ref(), std::cout << "model: " << m->ref() << "\n";
        else std::cout << m.error().message << "\n";
      } else if (cmd == "/models") print_models(app);
      else if (cmd == "/undo") {
        auto r = runner.undo(*s);
        std::cout << (r ? *r : r.error().message) << "\n";
      } else if (cmd == "/compact") {
        TerminalEvents ev;
        if (auto r = runner.compact(*s, ev); !r) std::cout << r.error().message << "\n";
      } else if (cmd == "/todos")
        for (auto& t : runner.todos()) std::cout << "  [" << t.status << "] " << t.content << "\n";
      else if (cmd == "/cost")
        std::cout << std::format("tokens in {} out {} (cached {}), ${:.4f}\n", s->usage.input, s->usage.output,
                                 s->usage.cache_read, s->cost);
      else std::cout << "unknown command; /help\n";
      continue;
    }
    TerminalEvents ev(o.reasoning);
    g_cancel = false;
    g_busy = true;
    auto r = runner.prompt(*s, line, ev, o.model);
    g_busy = false;
    ev.finish();
    if (g_cancel) std::cout << "(stopped)\n";
    else if (!r) std::cerr << "error: " << r.error().message << "\n";
  }
  return 0;
}

int debug(App& app, const std::vector<std::string>& args) {
  auto what = args.size() > 1 ? args[1] : "";
  if (what == "paths") {
    std::cout << "config   " << paths::config_dir().string() << "\ndata     " << paths::data_dir().string()
              << "\ncache    " << paths::cache_dir().string() << "\nproject  " << app.root.string()
              << "\nsessions " << app.store->dir().string() << "\n";
  } else if (what == "config") {
    std::cout << "sources:\n";
    for (auto& s : app.config.sources) std::cout << "  " << s.string() << "\n";
    std::cout << "merged:\n" << app.config.raw.dump(2) << "\n";
  } else if (what == "models") {
    print_models(app);
    auto d = app.providers->default_model();
    std::cout << "default: " << (d ? d->ref() : d.error().message) << "\nfallback chain:";
    for (auto& f : app.providers->free_fallbacks(d ? d->ref() : "")) std::cout << " " << f;
    std::cout << "\n";
  } else if (what == "agents") {
    for (auto* a : app.agents->list()) {
      Json tools = a->tools;
      std::cout << std::format("{} ({}, from {})\n  tools: {}\n  permission: {}\n", a->name,
                               a->mode == agent::Mode::subagent ? "subagent" : "primary", a->source, tools.dump(),
                               a->permission.dump());
    }
  } else if (what == "tools") {
    for (auto* t : app.tools.all()) std::cout << t->name() << "\n  " << t->description() << "\n  " << t->schema().dump() << "\n";
  } else if (what == "prompt") {
    auto* a = app.agents->find(args.size() > 2 ? args[2] : app.config.default_agent);
    if (!a) return std::cerr << "unknown agent\n", 1;
    auto m = app.providers->default_model();
    std::cout << session::system_prompt(*a, app.config, app.root, m ? m->ref() : "?") << "\n";
  } else if (what == "permission") {
    auto rules = permission::Rules::defaults();
    rules.push(permission::Rules::from_json(app.config.permission));
    if (args.size() > 3) {
      std::cout << permission::to_string(rules.evaluate(args[2], args[3])) << "\n";
    } else {
      std::cout << rules.to_json().dump(2) << "\n(check one: shaman debug permission bash \"git push\")\n";
    }
  } else if (what == "session") {
    auto s = args.size() > 2 ? app.store->get(args[2]) : app.store->latest() ? Result<session::Info>(*app.store->latest())
                                                                               : fail("no sessions");
    if (!s) return std::cerr << s.error().message << "\n", 1;
    std::cout << session::to_json(*s).dump(2) << "\n";
    if (auto ms = app.store->messages(s->id))
      for (auto& m : *ms) std::cout << llm::to_json(m).dump() << "\n";
  } else {
    std::cerr << "shaman debug <config|paths|models|agents|tools|prompt [agent]|permission [perm subject]|session [id]>\n";
    return 2;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  auto args = parse_args(argc, argv, {"continue", "yolo", "reasoning", "no-mcp", "help", "version", "refresh", "debug"},
                         {{"m", "model"}, {"a", "agent"}, {"c", "continue"}, {"s", "session"}, {"h", "help"},
                          {"v", "version"}});

  if (const char* env = std::getenv("SHAMAN_DEBUG"); env && *env) log::enable(log::parse_categories(env));
  if (args.has("debug")) log::enable(args.get("debug")->empty() ? unsigned(log::Cat::all) : log::parse_categories(*args.get("debug")));
  if (auto f = args.get("log-file")) log::set_file(*f);
  if (auto t = args.get("trace")) log::trace_open(*t);

  auto cmd = args.positional.empty() ? "" : args.positional[0];
  if (args.has("help") || cmd == "help") return std::cout << kHelp, 0;
  if (args.has("version") || cmd == "version") return std::cout << "shaman " << kVersion << "\n", 0;

  std::error_code ec;
  bool needs_mcp = cmd.empty() || cmd == "run";
  auto app = App::create(std::filesystem::current_path(ec), needs_mcp && !args.has("no-mcp"));
  if (!app) return std::cerr << "error: " << app.error().message << "\n", 1;

  Options o;
  o.model = args.get("model");
  o.agent = args.get("agent");
  o.session = args.get("session");
  o.cont = args.has("continue");
  o.yolo = args.has("yolo");
  o.reasoning = args.has("reasoning");
  o.json = args.get("format").value_or("text") == "json";

  std::signal(SIGINT, on_sigint);

  if (cmd.empty()) return repl(**app, o);
  if (cmd == "run") {
    std::vector<std::string> words(args.positional.begin() + 1, args.positional.end());
    return run_once(**app, o, str::join(words, " "));
  }
  if (cmd == "models") {
    if (args.has("refresh")) {
      auto r = provider::refresh_zen();
      if (!r) return std::cerr << "refresh failed: " << r.error().message << "\n", 1;
      auto fresh = App::create(std::filesystem::current_path(ec), false);
      if (fresh) print_models(**fresh);
      return 0;
    }
    return print_models(**app), 0;
  }
  if (cmd == "agents") {
    for (auto* a : (*app)->agents->list())
      std::cout << std::format("  {:<10} {:<9} {}\n", a->name, a->mode == agent::Mode::subagent ? "subagent" : "primary",
                               a->description);
    return 0;
  }
  if (cmd == "sessions") {
    for (auto& s : (*app)->store->list())
      std::cout << std::format("  {}  {:<8} {}\n", s.id, s.agent, s.title);
    return 0;
  }
  if (cmd == "debug") return debug(**app, args.positional);

  std::cerr << "unknown command: " << cmd << "\n\n" << kHelp;
  return 2;
}

}  // namespace shaman::cli
