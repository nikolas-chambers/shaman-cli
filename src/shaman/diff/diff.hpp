#pragma once

#include <string>

// Line diffs for showing edits (TUI, web UI, run output, session history).
namespace shaman::diff {

// Unified diff of two texts (Myers algorithm), with `context` lines around
// changes. Empty when the texts are identical.
std::string unified(const std::string& before, const std::string& after, const std::string& path, int context = 3);

struct Stats {
  int added = 0, removed = 0;
};
Stats stats(const std::string& unified_diff);

}  // namespace shaman::diff
