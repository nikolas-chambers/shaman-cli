#include "shaman/skill/skill.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::skill {
namespace fs = std::filesystem;

std::vector<Skill> discover(const fs::path& root) {
  std::vector<fs::path> dirs{paths::config_dir() / "skills"};
  if (const char* home = std::getenv("HOME")) dirs.push_back(fs::path(home) / ".claude" / "skills");
  dirs.push_back(root / ".claude" / "skills");
  dirs.push_back(root / ".shaman" / "skills");

  std::map<std::string, Skill> found;
  for (auto& dir : dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
      auto file = entry.path() / "SKILL.md";
      std::ifstream in(file);
      if (!in) continue;
      std::stringstream ss;
      ss << in.rdbuf();
      auto [fields, body] = str::front_matter(ss.str());
      Skill s{entry.path().filename().string(), "", entry.path(), str::trim(body)};
      for (auto& [k, v] : fields) {
        if (k == "name") s.name = v;
        if (k == "description") s.description = v;
      }
      log::debug(log::Cat::agent, "skill {} from {}", s.name, file.string());
      found[s.name] = std::move(s);
    }
  }
  std::vector<Skill> out;
  for (auto& [_, s] : found) out.push_back(std::move(s));
  return out;
}

}  // namespace shaman::skill
