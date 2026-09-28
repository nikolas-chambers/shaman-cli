#pragma once

#include <string>
#include <vector>

// Terminal rendering helpers: Markdown to ANSI-styled lines, word wrapping
// that ignores escape codes, and display width for UTF-8.
namespace shaman::tui {

size_t display_width(std::string_view s);  // code points, ANSI escapes excluded
std::vector<std::string> wrap(const std::string& styled, size_t width, const std::string& indent = "");

// Render Markdown (headings, lists, quotes, fenced code, **bold**, *italic*,
// `code`) to styled lines wrapped at `width`.
std::vector<std::string> render_markdown(const std::string& text, size_t width);

}  // namespace shaman::tui
