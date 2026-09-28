#include "shaman/session/system_prompt.hpp"

#include <chrono>
#include <format>
#include <fstream>
#include <sstream>

#include "shaman/agent/prompts.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"

namespace shaman::session {
namespace fs = std::filesystem;

static std::string platform() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

std::string system_prompt(const agent::Agent& agent, const Config& config, const fs::path& root,
                          const std::string& model_ref) {
  std::string out;
  if (agent.mode != agent::Mode::subagent) out += agent::prompts::base;
  if (!agent.prompt.empty()) out += (out.empty() ? "" : "\n\n") + agent.prompt;

  std::error_code ec;
  auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
  out += std::format("\n\n<env>\nworking directory: {}\ngit repository: {}\nplatform: {}\ndate: {:%Y-%m-%d}\nmodel: {}\n</env>",
                     root.string(), fs::exists(root / ".git", ec) ? "yes" : "no", platform(), today, model_ref);

  std::vector<fs::path> files;
  for (auto name : {"AGENTS.md", "SHAMAN.md", "CLAUDE.md"})
    if (fs::is_regular_file(root / name, ec)) {
      files.push_back(root / name);
      break;  // first match wins, like opencode
    }
  if (fs::is_regular_file(paths::config_dir() / "AGENTS.md", ec)) files.push_back(paths::config_dir() / "AGENTS.md");
  for (auto& extra : config.instructions) files.push_back(paths::resolve(root, extra));

  for (auto& f : files) {
    std::ifstream in(f);
    if (!in) {
      log::warn("instructions file not found: " + f.string());
      continue;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    log::debug(log::Cat::agent, "instructions: {}", f.string());
    out += std::format("\n\nInstructions from {}:\n{}", f.string(), ss.str());
  }
  return out;
}

}  // namespace shaman::session
