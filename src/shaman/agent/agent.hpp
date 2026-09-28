#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"
#include "shaman/permission/permission.hpp"

namespace shaman::agent {

enum class Mode { primary, subagent, all };

// An agent is a system prompt plus a tool set, permissions and model choice.
struct Agent {
  std::string name;
  std::string description;
  Mode mode = Mode::primary;
  std::string prompt;                  // appended to the base system prompt
  std::optional<std::string> model;    // overrides the session model
  std::optional<double> temperature;
  std::string reasoning_effort;  // low | medium | high; empty = the model's setting
  int max_steps = 100;
  std::map<std::string, bool> tools;   // tool -> enabled; missing means enabled
  Json permission = Json::object();    // layered on top of config permissions
  std::string source = "builtin";      // builtin | config | path to .md

  bool tool_enabled(const std::string& tool) const;
};

// Built-ins, then config "agent" entries, then markdown files from
// $XDG_CONFIG_HOME/shaman/agents/*.md and <project>/.shaman/agents/*.md.
// Later sources override earlier ones field by field.
class Registry {
 public:
  Registry(const Config& config, const std::filesystem::path& project_root);
  const Agent* find(const std::string& name) const;
  std::vector<const Agent*> list() const;

 private:
  void apply(const std::string& name, const Json& spec, const std::string& source);
  std::map<std::string, Agent> agents_;
};

}  // namespace shaman::agent
