#pragma once

#include <filesystem>
#include <string>

namespace shaman::paths {

namespace fs = std::filesystem;

// XDG base directories, each suffixed with "shaman".
fs::path config_dir();  // $XDG_CONFIG_HOME/shaman  (config, agents)
fs::path data_dir();    // $XDG_DATA_HOME/shaman    (sessions, snapshots)
fs::path cache_dir();   // $XDG_CACHE_HOME/shaman   (model catalog)

// Nearest ancestor containing a .git entry, or `cwd` itself.
fs::path project_root(const fs::path& cwd);

// Stable id for a project directory, used to namespace sessions.
std::string project_id(const fs::path& root);

// Resolve `p` against `base` and normalise it. Does not touch the filesystem.
fs::path resolve(const fs::path& base, const fs::path& p);

// True when `p` is `root` or lies beneath it (lexically).
bool within(const fs::path& root, const fs::path& p);

}  // namespace shaman::paths
