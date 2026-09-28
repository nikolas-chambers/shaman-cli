#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "shaman/llm/message.hpp"

namespace shaman::session {

struct Composed {
  llm::Message message;
  std::vector<std::filesystem::path> files;  // text files inlined (count as read)
  std::vector<std::string> warnings;
};

// Build a user message from typed text plus attachments.
//
// `@path` tokens that name an existing file or directory are expanded: text
// files are inlined, directories listed, images (png/jpg/gif/webp) attached as
// image parts. `attachments` (from --file) are always included.
Composed compose(const std::string& text, const std::vector<std::string>& attachments,
                 const std::filesystem::path& root);

std::string media_type(const std::filesystem::path& p);  // "" if not an image

}  // namespace shaman::session
