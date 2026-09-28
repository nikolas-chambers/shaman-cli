#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"
#include "shaman/core/result.hpp"

// Optional OS sandbox for the bash tool: commands can read everything but write only inside the project,
// the temp directory and any extra paths you list, optionally with no network.
//
//   "sandbox": true
//   "sandbox": { "network": false, "writable": ["~/.cache", "~/.npm"] }
//
// Linux uses bubblewrap (`bwrap`), macOS uses `sandbox-exec`. Windows has no equivalent yet. When the
// sandbox is on but cannot be set up, commands are refused rather than run unsandboxed.
namespace shaman::sandbox {

struct Settings {
  bool enabled = false;
  bool network = true;
  std::vector<std::filesystem::path> writable;  // beyond the project root and temp
};

Settings from_config(const Json& raw_config);

// A shell command line that runs `command` in the sandbox with `cwd` as working directory.
Result<std::string> wrap(const std::string& command, const std::filesystem::path& root,
                         const std::filesystem::path& cwd, const Settings& s);

// Which mechanism would be used here, or why none is available (for `shaman doctor`).
Result<std::string> mechanism();

}  // namespace shaman::sandbox
