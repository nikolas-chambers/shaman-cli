#pragma once

#include <string>
#include <vector>

#include "shaman/core/json.hpp"

namespace shaman::tui {

// Terminal UI colours as escape sequences. Presets, or your own colours as "#rrggbb" (24-bit terminals):
//   "tui": { "theme": "ocean" }   or   "tui": { "theme": "shaman", "accent": "#22e4f0", "code": "#8be9fd" }
struct Theme {
  std::string name = "shaman";
  std::string accent = "\x1b[35m";   // agent name, spinner, prompts
  std::string code = "\x1b[36m";     // inline code and code blocks
  std::string heading = "\x1b[1;35m";
  std::string add = "\x1b[32m";      // diff additions
  std::string del = "\x1b[31m";      // diff removals, errors
  std::string warn = "\x1b[33m";
  std::string info = "\x1b[36m";     // hunk headers, links
};

Theme& theme();                                   // the active theme
std::vector<std::string> theme_names();
bool set_theme(const std::string& name);          // a preset; false if unknown
void apply_theme_config(const Json& tui_config);  // "theme" plus colour overrides

}  // namespace shaman::tui
