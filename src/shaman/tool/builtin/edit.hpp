#pragma once

#include <string>

#include "shaman/core/result.hpp"

namespace shaman::tool {

// Replace `old_text` with `new_text` in `content`.
//
// Exact matching first. If that finds nothing, fall back to matching whole
// lines with leading/trailing whitespace ignored, which rescues the common
// case of a model getting indentation slightly wrong. Ambiguous matches are
// rejected unless `replace_all` is set, so edits never land in the wrong place.
Result<std::string> apply_edit(const std::string& content, const std::string& old_text,
                               const std::string& new_text, bool replace_all);

}  // namespace shaman::tool
