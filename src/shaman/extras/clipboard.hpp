#pragma once

#include <filesystem>

#include "shaman/core/result.hpp"

namespace shaman::extras {

// Save an image on the system clipboard as PNG in `dir`. Uses wl-paste or xclip on Linux, osascript on
// macOS and PowerShell on Windows. Fails when the clipboard holds no image or no tool is available.
Result<std::filesystem::path> paste_image(const std::filesystem::path& dir);

// Put text on the system clipboard (wl-copy, xclip, pbcopy, clip, termux-clipboard-set). Returns the tool used.
Result<std::string> copy_text(const std::string& text);

}  // namespace shaman::extras
