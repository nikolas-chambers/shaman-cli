#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace shaman::skill {

// A skill is a folder with a SKILL.md: front matter (name, description) and
// instructions, plus any scripts or references it mentions. The model sees the
// list of names and descriptions and loads one with the `skill` tool when it
// is relevant, so skills cost no context until used.
//
// Searched, later overriding earlier:
//   $XDG_CONFIG_HOME/shaman/skills/*/SKILL.md
//   ~/.claude/skills/*/SKILL.md            (compatible with Claude Code skills)
//   <project>/.claude/skills/*/SKILL.md
//   <project>/.shaman/skills/*/SKILL.md
struct Skill {
  std::string name;
  std::string description;
  std::filesystem::path dir;
  std::string body;
};

std::vector<Skill> discover(const std::filesystem::path& project_root);

}  // namespace shaman::skill
