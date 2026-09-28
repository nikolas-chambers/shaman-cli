#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "shaman/cli/app.hpp"
#include "shaman/cli/args.hpp"

namespace shaman::cli {

struct Options {
  std::optional<std::string> model, agent, session, command, attach;
  std::vector<std::string> files;
  bool cont = false, yolo = false, reasoning = false, json = false, plain = false;
};

Options options_from(const Args& args);
extern std::atomic<bool> g_cancel;
extern std::atomic<bool> g_busy;

int cmd_run(App& app, const Options& o, std::string message);
int cmd_attach(const Options& o, std::string message);
int cmd_repl(App& app, Options o);  // line-based fallback when no TTY or --plain
int cmd_models(App& app, const Args& args);
int cmd_auth(App& app, const Args& args);
int cmd_agents(App& app, const Args& args);
int cmd_sessions(App& app, const Args& args);
int cmd_export(App& app, const Args& args);
int cmd_import(App& app, const Args& args);
int cmd_share(App& app, const Args& args);
int cmd_stats(App& app, const Args& args);
int cmd_mcp(App& app, const Args& args);
int cmd_plugins(App& app, const Args& args);
int cmd_pr(App& app, const Options& o, const Args& args);
int cmd_debug(App& app, const Args& args);

void print_models(App& app, bool all);
Result<session::Info> open_session(App& app, const Options& o);

}  // namespace shaman::cli
