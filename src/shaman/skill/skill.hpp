#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shaman::skill {

// A skill is a folder with a SKILL.md: front matter (name, description) and
// instructions, plus any scripts, templates or references it mentions. The
// model sees names and descriptions and loads one with the `skill` tool when
// relevant, so skills cost no context until used.
//
// Sources, later overriding earlier:
//   built-in skills compiled into shaman (skills/ in the source tree), unpacked
//     to $XDG_CACHE_HOME/shaman/skills/<content-hash>/ so their scripts can run
//   $XDG_CONFIG_HOME/shaman/skills/**/SKILL.md
//   <project>/.shaman/skills/**/SKILL.md
// Folders are searched up to 4 levels deep, so skills can be grouped.
struct Skill {
  std::string name;
  std::string description;
  std::filesystem::path dir;
  std::string body;
  std::string source;  // "built-in" or the SKILL.md path
};

// Cached per project root; cheap to call repeatedly.
const std::vector<Skill>& discover(const std::filesystem::path& project_root);

}  // namespace shaman::skill

namespace shaman::embedded {
struct SkillFile {
  std::string_view skill, path, data;
};
const std::vector<SkillFile>& skill_files();
}  // namespace shaman::embedded
