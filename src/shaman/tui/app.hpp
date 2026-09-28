#pragma once

#include <optional>
#include <string>

#include "shaman/cli/app.hpp"

// Full-screen terminal UI: scrollable transcript with Markdown rendering,
// multi-line input with history and completion, status bar, permission
// dialogs, and a command palette (Ctrl-P) for sessions, models and agents.
namespace shaman::tui {

struct Options {
  std::optional<std::string> model, agent, session, prompt;
  bool cont = false, allow_all = false, accept_edits = false;
  std::string effort;
};

int run(cli::App& app, const Options& opts);

}  // namespace shaman::tui
