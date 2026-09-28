#include "shaman/core/paths.hpp"

#include <cstdlib>

#include "shaman/core/strings.hpp"

namespace shaman::paths {

[[maybe_unused]] static fs::path xdg(const char* var, const char* fallback) {
  if (const char* v = std::getenv(var); v && *v) return fs::path(v) / "shaman";
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / fallback / "shaman";
}

#ifdef _WIN32
static fs::path win(const char* var) {
  const char* v = std::getenv(var);
  return fs::path(v ? v : ".") / "shaman";
}
fs::path config_dir() { return win("APPDATA"); }
fs::path data_dir() { return win("LOCALAPPDATA"); }
fs::path cache_dir() { return win("LOCALAPPDATA") / "cache"; }
#else
fs::path config_dir() { return xdg("XDG_CONFIG_HOME", ".config"); }
fs::path data_dir() { return xdg("XDG_DATA_HOME", ".local/share"); }
fs::path cache_dir() { return xdg("XDG_CACHE_HOME", ".cache"); }
#endif

fs::path project_root(const fs::path& cwd) {
  std::error_code ec;
  for (fs::path dir = fs::absolute(cwd, ec); !dir.empty(); dir = dir.parent_path()) {
    if (fs::exists(dir / ".git", ec)) return dir;
    if (dir == dir.root_path()) break;
  }
  return fs::absolute(cwd, ec);
}

std::string project_id(const fs::path& root) { return str::hash_hex(root.generic_string()); }

fs::path resolve(const fs::path& base, const fs::path& p) {
  return (p.is_absolute() ? p : base / p).lexically_normal();
}

bool within(const fs::path& root, const fs::path& p) {
  auto rel = p.lexically_normal().lexically_relative(root.lexically_normal());
  return !rel.empty() && *rel.begin() != "..";
}

}  // namespace shaman::paths
