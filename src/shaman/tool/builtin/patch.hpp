#pragma once

#include <string>
#include <vector>

#include "shaman/core/result.hpp"

namespace shaman::tool {

// The "apply_patch" format used by opencode and Codex:
//
//   *** Begin Patch
//   *** Add File: path/new.txt
//   +line
//   *** Update File: path/old.txt
//   *** Move to: path/renamed.txt        (optional)
//   @@ optional anchor line
//    context
//   -removed
//   +added
//   *** Delete File: path/gone.txt
//   *** End Patch
struct Hunk {
  std::string anchor;               // text after "@@", may be empty
  std::vector<std::string> before;  // context + removed lines
  std::vector<std::string> after;   // context + added lines
};

struct FileOp {
  enum Kind { add, update, remove } kind;
  std::string path;
  std::string move_to;
  std::string content;  // for add
  std::vector<Hunk> hunks;
};

Result<std::vector<FileOp>> parse_patch(const std::string& text);

// Apply update hunks to file content. Each hunk is located after the previous
// one (exact match first, then ignoring surrounding whitespace).
Result<std::string> apply_hunks(const std::string& content, const std::vector<Hunk>& hunks);

}  // namespace shaman::tool
