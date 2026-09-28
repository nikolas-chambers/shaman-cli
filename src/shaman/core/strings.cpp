#include "shaman/core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <regex>

namespace shaman::str {

std::string trim(std::string_view s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string_view::npos) return {};
  auto e = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(b, e - b + 1));
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::vector<std::string> lines(std::string_view s) {
  auto out = split(s, '\n');
  for (auto& l : out)
    if (!l.empty() && l.back() == '\r') l.pop_back();
  if (!out.empty() && out.back().empty()) out.pop_back();
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

size_t count(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return 0;
  size_t n = 0;
  for (size_t pos = haystack.find(needle); pos != std::string_view::npos;
       pos = haystack.find(needle, pos + needle.size()))
    ++n;
  return n;
}

bool wildcard(std::string_view p, std::string_view t) {
  size_t pi = 0, ti = 0, star = std::string_view::npos, mark = 0;
  while (ti < t.size()) {
    if (pi < p.size() && p[pi] == '*') {
      star = pi++;
      mark = ti;
    } else if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
      ++pi, ++ti;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      ti = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == '*') ++pi;
  return pi == p.size();
}

static std::string glob_to_regex(std::string_view g) {
  std::string re;
  bool in_brace = false;
  for (size_t i = 0; i < g.size(); ++i) {
    char c = g[i];
    if (c == '*') {
      if (i + 1 < g.size() && g[i + 1] == '*') {
        bool slash = i + 2 < g.size() && g[i + 2] == '/';
        re += slash ? "(?:.*/)?" : ".*";
        i += slash ? 2 : 1;
      } else {
        re += "[^/]*";
      }
    } else if (c == '?') {
      re += "[^/]";
    } else if (c == '{') {
      in_brace = true;
      re += "(?:";
    } else if (c == '}' && in_brace) {
      in_brace = false;
      re += ")";
    } else if (c == ',' && in_brace) {
      re += "|";
    } else if (std::string_view(".+()|^$[]\\").find(c) != std::string_view::npos) {
      re += '\\';
      re += c;
    } else {
      re += c;
    }
  }
  return re;
}

bool glob(std::string_view pattern, std::string_view path) {
  // Patterns without a slash match the basename anywhere, like ripgrep's --glob.
  std::string p(pattern);
  if (p.find('/') == std::string::npos) p = "**/" + p;
  return std::regex_match(std::string(path), std::regex(glob_to_regex(p)));
}

std::string hash_hex(std::string_view s) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return std::format("{:016x}", h);
}

}  // namespace shaman::str
