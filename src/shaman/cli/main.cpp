#include "shaman/cli/main.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>

#include "shaman/acp/acp.hpp"
#include "shaman/cli/commands.hpp"
#include "shaman/cli/render.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include <set>
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/github/github.hpp"
#include "shaman/mcp/mcp.hpp"
#include "shaman/server/server.hpp"
#include "shaman/tui/app.hpp"
#include "shaman/update/update.hpp"
#include "shaman/version.hpp"

namespace shaman::cli {

std::atomic<bool> g_cancel{false};
std::atomic<bool> g_busy{false};

namespace {

void on_sigint(int) {
  if (g_busy.load()) g_cancel = true;  // first ^C stops the turn
  else std::_Exit(130);                 // idle: quit
}

constexpr const char* kHelp = R"(shaman - a coding agent for your terminal

usage:
  shaman [message]                 full-screen UI (optionally starting with a message)
  shaman run [message]             one-shot; also reads piped stdin
  shaman serve                     HTTP API + web UI        shaman web     serve and open the browser
  shaman acp                       Agent Client Protocol on stdio (Zed and other editors)

  shaman models [--refresh] [--all]            shaman auth login|list|logout [provider]
  shaman agents | agent create <name>          shaman sessions
  shaman export [id] [--format md|json|html] [-o file]
  shaman import <file>   shaman share [id]     shaman stats [--days N]
  shaman mcp list|auth <name>|logout <name>|serve
  shaman plugins                               shaman pr <number>
  shaman github install|run [--dry-run]        shaman upgrade [version] [--check]
  shaman debug config|paths|models|agents|tools|prompt|permission|session|lsp|skills|commands|plugins
  shaman version

options:
  -m, --model <provider/model>     model (default: best free model)
  -a, --agent <name>               agent (default: build)
  -c, --continue                   continue the most recent session
  -s, --session <id>               continue a specific session
  -f, --file <path>                attach a file or image (repeatable via @mentions too)
      --command <name>             run a custom command (message becomes its arguments)
      --attach <url>               run against a `shaman serve` instance
      --format <text|json>         run output format
      --yolo                       allow every tool call without asking
      --reasoning                  show model reasoning when available
      --plain                      line-based interface instead of full-screen
      --no-mcp                     skip MCP servers and plugins
      --port <n> --host <h> --token <t>   serve/web options
      --debug[=cats]               debug log: all, or config,provider,http,sse,tool,permission,session,agent,mcp
      --log-file <path>            write debug log to a file instead of stderr
      --trace <path>               record every request/event/tool call as JSONL

environment:
  SHAMAN_DEBUG, SHAMAN_MODEL, SHAMAN_CONFIG, SHAMAN_CONFIG_CONTENT, OPENCODE_API_KEY, ANTHROPIC_API_KEY, ...
)";

void open_browser(const std::string& url) {
#if defined(__APPLE__)
  process::shell("open '" + url + "'", {.timeout = std::chrono::seconds(5)});
#elif defined(_WIN32)
  process::shell("start \"\" \"" + url + "\"", {.timeout = std::chrono::seconds(5)});
#else
  process::shell("xdg-open '" + url + "' >/dev/null 2>&1 &", {.timeout = std::chrono::seconds(5)});
#endif
}

}  // namespace

Options options_from(const Args& args) {
  Options o;
  o.model = args.get("model");
  o.agent = args.get("agent");
  o.session = args.get("session");
  o.command = args.get("command");
  o.attach = args.get("attach");
  if (auto f = args.get("file")) o.files = str::split(*f, ',');
  o.cont = args.has("continue");
  o.yolo = args.has("yolo");
  o.reasoning = args.has("reasoning");
  o.json = args.get("format").value_or("text") == "json";
  o.plain = args.has("plain");
  return o;
}

int main(int argc, char** argv) {
  auto args = parse_args(argc, argv,
                         {"continue", "yolo", "reasoning", "no-mcp", "help", "version", "refresh", "debug", "plain",
                          "all", "check", "dry-run", "verbose"},
                         {{"m", "model"}, {"a", "agent"}, {"c", "continue"}, {"s", "session"}, {"h", "help"},
                          {"v", "version"}, {"f", "file"}, {"o", "output"}});

  if (const char* env = std::getenv("SHAMAN_DEBUG"); env && *env) log::enable(log::parse_categories(env));
  if (args.has("debug"))
    log::enable(args.get("debug")->empty() ? unsigned(log::Cat::all) : log::parse_categories(*args.get("debug")));
  if (auto f = args.get("log-file")) log::set_file(*f);
  if (auto t = args.get("trace")) log::trace_open(*t);

  auto& pos = args.positional;
  std::string cmd = pos.empty() ? "" : pos[0];
  if (args.has("help") || cmd == "help") return std::cout << kHelp, 0;
  if (args.has("version") || cmd == "version") return std::cout << "shaman " << kVersion << "\n", 0;
  if (cmd == "upgrade") return update::upgrade(pos.size() > 1 ? pos[1] : "", args.has("check"));
  if (cmd == "github" && pos.size() > 1 && pos[1] == "install") {
    std::error_code ec;
    return github::install(paths::project_root(std::filesystem::current_path(ec)));
  }

  Options o = options_from(args);
  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);
  if (cmd == "acp") return acp::serve(cwd, {.allow_all = o.yolo});
  if (cmd == "run" && o.attach) return cmd_attach(o, str::join({pos.begin() + 1, pos.end()}, " "));

  static const std::set<std::string> known{"run", "serve", "web", "models", "auth", "agents", "agent", "sessions",
                                           "export", "import", "share", "stats", "mcp", "plugins", "pr", "github", "debug"};
  bool interactive = cmd.empty() || !known.contains(cmd);
  bool runtime = (interactive || cmd == "run" || cmd == "serve" || cmd == "web" || cmd == "pr" || cmd == "github" ||
                  (cmd == "mcp" && pos.size() > 1 && pos[1] == "serve")) && !args.has("no-mcp");
  auto app = App::create(cwd, runtime);
  if (!app) return std::cerr << "error: " << app.error().message << "\n", 1;
  auto& a = **app;

  std::signal(SIGINT, on_sigint);

  if (interactive) {
    // Anything that isn't a subcommand is an opening message: `shaman fix the tests`.
    std::optional<std::string> first;
    if (!pos.empty()) first = str::join(pos, " ");
    if (o.plain || !stdin_is_tty() || !stdout_is_tty()) {
      if (first) return cmd_run(a, o, *first);
      return cmd_repl(a, o);
    }
    std::signal(SIGINT, SIG_IGN);  // the TUI handles Ctrl-C itself
    return tui::run(a, {o.model, o.agent, o.session, first, o.cont, o.yolo});
  }
  if (cmd == "run") return cmd_run(a, o, str::join({pos.begin() + 1, pos.end()}, " "));
  if (cmd == "serve" || cmd == "web") {
    server::Options so;
    so.host = args.get("host").value_or("127.0.0.1");
    so.port = std::stoi(args.get("port").value_or(cmd == "web" ? "0" : "4096"));
    so.token = args.get("token").value_or("");
    so.allow_all = o.yolo;
    so.on_listen = [web = cmd == "web"](const std::string& url) {
      if (web) open_browser(url);
    };
    return server::serve(a, so);
  }
  if (cmd == "models") return cmd_models(a, args);
  if (cmd == "auth") return cmd_auth(a, args);
  if (cmd == "agents" || cmd == "agent") return cmd_agents(a, args);
  if (cmd == "sessions") return cmd_sessions(a, args);
  if (cmd == "export") return cmd_export(a, args);
  if (cmd == "import") return cmd_import(a, args);
  if (cmd == "share") return cmd_share(a, args);
  if (cmd == "stats") return cmd_stats(a, args);
  if (cmd == "mcp") return cmd_mcp(a, args);
  if (cmd == "plugins") return cmd_plugins(a, args);
  if (cmd == "pr") return cmd_pr(a, o, args);
  if (cmd == "github") {
    if (pos.size() > 1 && pos[1] == "run") return github::run(a, args.has("dry-run"));
    return std::cerr << "usage: shaman github install|run [--dry-run]\n", 2;
  }
  if (cmd == "debug") return cmd_debug(a, args);
  return 2;
}

}  // namespace shaman::cli
