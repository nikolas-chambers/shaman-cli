#pragma once

#include <filesystem>
#include <string>

#include "shaman/config/config.hpp"

namespace shaman::session {

// Run the project's formatter on a file the agent just wrote, like opencode.
// Unlike opencode, a built-in formatter only runs when the project is set up
// for it (a .clang-format, prettier config, ruff/black in pyproject, go.mod,
// Cargo.toml...), so shaman never reformats code in a style nobody chose.
//
// Config: "formatter": false | { "<name>": { "command": ["fmt", "$FILE"], "extensions": [".x"] } | false }
// Returns the formatter's name, or "" if none ran.
std::string format_file(const Config& config, const std::filesystem::path& root, const std::filesystem::path& file);

}  // namespace shaman::session
