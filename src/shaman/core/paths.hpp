#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace shaman::paths {

namespace fs = std::filesystem;

// Where shaman keeps its files. Normally the per-user XDG directories (Windows: %APPDATA% and
// %LOCALAPPDATA%). In portable mode everything lives in folders next to the executable instead:
//   <dir>/shaman.ini  keys and settings     <dir>/config/  config, agents, commands, skills
//   <dir>/data/       sessions, snapshots   <dir>/cache/   model catalog
// Portable mode is on when $SHAMAN_HOME is set (that directory is used), or when a `shaman.ini` or
// `portable` file sits next to the executable.
fs::path config_dir();  // $XDG_CONFIG_HOME/shaman  (config, agents)
fs::path data_dir();    // $XDG_DATA_HOME/shaman    (sessions, snapshots)
fs::path cache_dir();   // $XDG_CACHE_HOME/shaman   (model catalog)

fs::path executable();                     // absolute path of the running binary (empty if unknown)
std::optional<fs::path> portable_root();   // set in portable mode

// Nearest ancestor containing a .git entry, or `cwd` itself.
fs::path project_root(const fs::path& cwd);

// Stable id for a project directory, used to namespace sessions.
std::string project_id(const fs::path& root);

// Resolve `p` against `base` and normalise it. Does not touch the filesystem.
fs::path resolve(const fs::path& base, const fs::path& p);

// True when `p` is `root` or lies beneath it (lexically).
bool within(const fs::path& root, const fs::path& p);

}  // namespace shaman::paths
