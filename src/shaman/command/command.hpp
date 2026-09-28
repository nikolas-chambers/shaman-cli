#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"

namespace shaman::command {

// Custom slash commands: reusable prompt templates.
//
// Markdown files in $XDG_CONFIG_HOME/shaman/commands/ or .shaman/commands/
// (name = file stem), or config:
//   "command": { "test": { "template": "Run the tests and fix failures in $ARGUMENTS",
//                          "description": "...", "agent": "build", "model": "..." } }
//
// Templates support $ARGUMENTS, $1..$9, !`shell command` (replaced by its
// output) and @file mentions (expanded like typed input).
struct Command {
  std::string name;
  std::string description;
  std::string body;
  std::optional<std::string> agent;
  std::optional<std::string> model;
  std::string source;
  // Commands whose text comes from elsewhere (MCP prompts): called with the arguments instead of `body`.
  std::function<std::string(const std::string& arguments)> fetch;
};

std::vector<Command> discover(const Config& config, const std::filesystem::path& root);
const Command* find(const std::vector<Command>& commands, const std::string& name);

// Fill a template. Shell substitutions run in `root` with a 30 s timeout.
std::string expand(const Command& cmd, const std::string& arguments, const std::filesystem::path& root);

}  // namespace shaman::command
