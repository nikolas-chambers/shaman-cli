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

// Percent-encoding for URLs (RFC 3986 unreserved characters are kept).
std::string url_encode(std::string_view s);
std::string url_decode(std::string_view s);

std::string base64_encode(std::string_view data);

// Minimal HTML entity decoding (&amp; &lt; &gt; &quot; &#39; &nbsp; &#NN;).
std::string html_unescape(std::string s);

// Parse "---\nkey: value\n---\nbody" front matter. Returns (fields, body).
std::pair<std::vector<std::pair<std::string, std::string>>, std::string> front_matter(std::string_view text);

// Stable 64-bit FNV-1a, hex encoded. Used for project ids.
std::string hash_hex(std::string_view s);

}  // namespace shaman::str
