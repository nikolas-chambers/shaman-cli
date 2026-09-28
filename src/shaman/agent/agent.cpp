#include "shaman/agent/agent.hpp"

#include <fstream>
#include <sstream>

#include "shaman/agent/prompts.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::agent {
namespace fs = std::filesystem;

bool Agent::tool_enabled(const std::string& tool) const {
  auto it = tools.find(tool);
  if (it != tools.end()) return it->second;
  auto star = tools.find("*");
  return star == tools.end() || star->second;
}

namespace {

Mode parse_mode(const std::string& s) {
  return s == "subagent" ? Mode::subagent : s == "all" ? Mode::all : Mode::primary;
}

// Minimal front matter: "key: value" lines between --- fences. Values are
// taken as strings, except `tools: a, b, !c` which becomes an enable map.
Json parse_markdown_agent(const std::string& text) {
  Json spec = Json::object();
  std::string body = text;
  if (text.starts_with("---")) {
    auto end = text.find("\n---", 3);
    if (end != std::string::npos) {
      for (auto& line : str::lines(text.substr(3, end - 3))) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto key = str::trim(line.substr(0, colon)), value = str::trim(line.substr(colon + 1));
        if (key == "tools") {
          Json tools = {{"*", false}};
          for (auto& t : str::split(value, ',')) {
            auto name = str::trim(t);
            if (name.starts_with("!")) tools[name.substr(1)] = false;
            else if (!name.empty()) tools[name] = true;
          }
          spec["tools"] = tools;
        } else if (key == "temperature") {
          spec[key] = std::stod(value);
        } else {
          spec[key] = value;
        }
      }
      body = text.substr(text.find('\n', end + 1) + 1);
    }
  }
  spec["prompt"] = str::trim(body);
  return spec;
}

}  // namespace

Registry::Registry(const Config& config, const fs::path& root) {
  apply("build", {{"description", "Default agent. Full tool access; edits and commands ask first."}}, "builtin");
  apply("plan", {{"description", "Read-only planning. Explores and proposes, never edits."},
                 {"prompt", prompts::plan},
                 {"tools", {{"write", false}, {"edit", false}, {"apply_patch", false}, {"multiedit", false}, {"notebook_edit", false}}},
                 {"permission", {{"edit", "deny"}, {"bash", {{"*", "deny"}, {"git diff*", "allow"}, {"git log*", "allow"},
                                                              {"git status*", "allow"}, {"ls*", "allow"}}}}}},
        "builtin");
  apply("explore", {{"description", "Fast read-only codebase search."}, {"mode", "subagent"},
                    {"prompt", prompts::explore}, {"max_steps", 40},
                    {"tools", {{"*", false}, {"read", true}, {"glob", true}, {"grep", true}, {"list", true}, {"batch", true}, {"lsp", true}}}},
        "builtin");
  apply("general", {{"description", "General-purpose subagent for multi-step tasks."}, {"mode", "subagent"},
                    {"prompt", prompts::general},
                    {"tools", {{"task", false}, {"task_output", false}, {"todowrite", false}, {"todoread", false}, {"question", false}}}},
        "builtin");

  for (auto& [name, spec] : config.agent.items()) apply(name, spec, "config");

  for (auto dir : {paths::config_dir() / "agents", root / ".shaman" / "agents"}) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
      if (entry.path().extension() != ".md") continue;
      std::ifstream in(entry.path());
      std::stringstream ss;
      ss << in.rdbuf();
      try {
        apply(entry.path().stem().string(), parse_markdown_agent(ss.str()), entry.path().string());
      } catch (const std::exception& e) {
        log::warn("agent " + entry.path().string() + ": " + e.what());
      }
    }
  }
}

void Registry::apply(const std::string& name, const Json& spec, const std::string& source) {
  auto& a = agents_[name];
  a.name = name;
  a.source = source;
  if (spec.contains("description")) a.description = spec["description"];
  if (spec.contains("mode")) a.mode = parse_mode(spec["mode"]);
  if (spec.contains("prompt")) a.prompt = spec["prompt"];
  if (spec.contains("model")) a.model = spec["model"].get<std::string>();
  if (spec.contains("temperature")) a.temperature = spec["temperature"].get<double>();
  if (spec.contains("max_steps")) a.max_steps = spec["max_steps"];
  if (spec.contains("tools"))
    for (auto& [tool, on] : spec["tools"].items()) a.tools[tool] = on.get<bool>();
  if (spec.contains("permission")) a.permission.merge_patch(spec["permission"]);
  log::debug(log::Cat::agent, "agent {} from {}", name, source);
}

const Agent* Registry::find(const std::string& name) const {
  auto it = agents_.find(name);
  return it == agents_.end() ? nullptr : &it->second;
}

std::vector<const Agent*> Registry::list() const {
  std::vector<const Agent*> out;
  for (auto& [_, a] : agents_) out.push_back(&a);
  return out;
}

}  // namespace shaman::agent
