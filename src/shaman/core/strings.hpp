#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace shaman::str {

std::string trim(std::string_view s);
std::string lower(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep);
std::vector<std::string> lines(std::string_view s);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
std::string replace_all(std::string s, std::string_view from, std::string_view to);
size_t count(std::string_view haystack, std::string_view needle);

// Shell-style wildcard over a flat string: '*' matches anything, '?' one char.
bool wildcard(std::string_view pattern, std::string_view text);

// Path glob: '**' crosses directories, '*' and '?' do not, '{a,b}' alternates.
bool glob(std::string_view pattern, std::string_view path);

// Stable 64-bit FNV-1a, hex encoded. Used for project ids.
std::string hash_hex(std::string_view s);

}  // namespace shaman::str
