#include "shaman/core/paths.hpp"

#include <cstdlib>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "shaman/core/strings.hpp"

namespace shaman::paths {

[[maybe_unused]] static fs::path xdg(const char* var, const char* fallback) {
  if (const char* v = std::getenv(var); v && *v) return fs::path(v) / "shaman";
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / fallback / "shaman";
}

fs::path executable() {
#ifdef _WIN32
  wchar_t buf[32768];
  DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
  return n ? fs::path(std::wstring(buf, n)) : fs::path();
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) return {};
  std::error_code ec;
  auto p = fs::canonical(buf, ec);
  return ec ? fs::path(buf) : p;
#else
  std::error_code ec;
  auto p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : p;
#endif
}

std::optional<fs::path> portable_root() {
  static const std::optional<fs::path> root = []() -> std::optional<fs::path> {
    if (const char* home = std::getenv("SHAMAN_HOME"); home && *home) return fs::absolute(home);
    auto exe = executable();
    if (exe.empty()) return std::nullopt;
    auto dir = exe.parent_path();
    std::error_code ec;
    if (fs::exists(dir / "shaman.ini", ec) || fs::exists(dir / "portable", ec)) return dir;
    return std::nullopt;
  }();
  return root;
}

#ifdef _WIN32
static fs::path win(const char* var) {
  const char* v = std::getenv(var);
  return fs::path(v ? v : ".") / "shaman";
}
fs::path config_dir() { return portable_root() ? *portable_root() / "config" : win("APPDATA"); }
fs::path data_dir() { return portable_root() ? *portable_root() / "data" : win("LOCALAPPDATA"); }
fs::path cache_dir() { return portable_root() ? *portable_root() / "cache" : win("LOCALAPPDATA") / "cache"; }
#else
fs::path config_dir() { return portable_root() ? *portable_root() / "config" : xdg("XDG_CONFIG_HOME", ".config"); }
fs::path data_dir() { return portable_root() ? *portable_root() / "data" : xdg("XDG_DATA_HOME", ".local/share"); }
fs::path cache_dir() { return portable_root() ? *portable_root() / "cache" : xdg("XDG_CACHE_HOME", ".cache"); }
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
