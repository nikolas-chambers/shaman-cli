#include "shaman/tui/theme.hpp"

#include <cstdio>
#include <format>
#include <map>

namespace shaman::tui {

namespace {
std::map<std::string, Theme> presets() {
  std::map<std::string, Theme> m;
  auto rgb = [](int r, int g, int b) { return std::format("\x1b[38;2;{};{};{}m", r, g, b); };
  auto bold = [](const std::string& c) { return "\x1b[1m" + c; };
  m["shaman"] = Theme{};
  m["dark"] = {"dark", rgb(34, 228, 240), rgb(139, 233, 253), bold(rgb(34, 228, 240)), rgb(111, 217, 138), rgb(255, 138, 128),
               rgb(255, 214, 102), rgb(139, 233, 253)};
  m["halloween"] = {"halloween", rgb(255, 140, 26), rgb(190, 130, 255), bold(rgb(255, 140, 26)), rgb(160, 220, 90),
                    rgb(255, 72, 72), rgb(255, 196, 0), rgb(190, 130, 255)};
  m["dracula"] = {"dracula", rgb(255, 121, 198), rgb(139, 233, 253), bold(rgb(189, 147, 249)), rgb(80, 250, 123),
                  rgb(255, 85, 85), rgb(241, 250, 140), rgb(139, 233, 253)};
  m["nord"] = {"nord", rgb(136, 192, 208), rgb(143, 188, 187), bold(rgb(129, 161, 193)), rgb(163, 190, 140), rgb(191, 97, 106),
               rgb(235, 203, 139), rgb(94, 129, 172)};
  m["gruvbox"] = {"gruvbox", rgb(254, 128, 25), rgb(142, 192, 124), bold(rgb(250, 189, 47)), rgb(184, 187, 38),
                  rgb(251, 73, 52), rgb(250, 189, 47), rgb(131, 165, 152)};
  m["solarized"] = {"solarized", rgb(38, 139, 210), rgb(42, 161, 152), bold(rgb(181, 137, 0)), rgb(133, 153, 0),
                    rgb(220, 50, 47), rgb(203, 75, 22), rgb(108, 113, 196)};
  m["monokai"] = {"monokai", rgb(249, 38, 114), rgb(102, 217, 239), bold(rgb(166, 226, 46)), rgb(166, 226, 46),
                  rgb(249, 38, 114), rgb(230, 219, 116), rgb(174, 129, 255)};
  m["ocean"] = {"ocean", "\x1b[34m", "\x1b[36m", "\x1b[1;34m", "\x1b[32m", "\x1b[31m", "\x1b[33m", "\x1b[36m"};
  m["forest"] = {"forest", "\x1b[32m", "\x1b[33m", "\x1b[1;32m", "\x1b[32m", "\x1b[31m", "\x1b[33m", "\x1b[36m"};
  m["sunset"] = {"sunset", "\x1b[38;5;208m", "\x1b[38;5;175m", "\x1b[1;38;5;208m", "\x1b[38;5;114m", "\x1b[38;5;203m",
                 "\x1b[38;5;221m", "\x1b[38;5;110m"};
  m["mono"] = {"mono", "\x1b[1m", "\x1b[2m", "\x1b[1;4m", "\x1b[1m", "\x1b[7m", "\x1b[1m", "\x1b[2m"};
  // Darker variants that stay readable on white terminals.
  m["light"] = {"light", "\x1b[35m", "\x1b[34m", "\x1b[1;35m", "\x1b[32m", "\x1b[31m", "\x1b[38;5;130m", "\x1b[34m"};
  return m;
}

std::string hex_to_escape(const std::string& hex, const std::string& fallback) {
  unsigned r, g, b;
  if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
    return "\x1b[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
  return fallback;
}
}  // namespace

Theme& theme() {
  static Theme t;
  return t;
}

std::vector<std::string> theme_names() {
  std::vector<std::string> out;
  for (auto& [name, _] : presets()) out.push_back(name);
  return out;
}

bool set_theme(const std::string& name) {
  auto all = presets();
  auto it = all.find(name);
  if (it == all.end()) return false;
  theme() = it->second;
  return true;
}

void apply_theme_config(const Json& c) {
  if (!c.is_object()) return;
  if (auto n = c.value("theme", ""); !n.empty()) set_theme(n);
  auto& t = theme();
  for (auto [key, field] : std::initializer_list<std::pair<const char*, std::string*>>{
           {"accent", &t.accent}, {"code", &t.code}, {"heading", &t.heading}, {"add", &t.add},
           {"del", &t.del}, {"warn", &t.warn}, {"info", &t.info}})
    if (c.contains(key) && c[key].is_string()) *field = hex_to_escape(c[key].get<std::string>(), *field);
}

}  // namespace shaman::tui
