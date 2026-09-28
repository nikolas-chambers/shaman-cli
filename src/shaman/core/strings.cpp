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

std::string url_encode(std::string_view s) {
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
    else out += std::format("%{:02X}", c);
  }
  return out;
}

std::string url_decode(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += char(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
      i += 2;
    } else {
      out += s[i] == '+' ? ' ' : s[i];
    }
  }
  return out;
}

std::string base64_encode(std::string_view data) {
  static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t n = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8) | uint8_t(data[i + 2]);
    out += tbl[n >> 18], out += tbl[(n >> 12) & 63], out += tbl[(n >> 6) & 63], out += tbl[n & 63];
  }
  if (i + 1 == data.size()) {
    uint32_t n = uint8_t(data[i]) << 16;
    out += tbl[n >> 18], out += tbl[(n >> 12) & 63], out += "==";
  } else if (i + 2 == data.size()) {
    uint32_t n = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8);
    out += tbl[n >> 18], out += tbl[(n >> 12) & 63], out += tbl[(n >> 6) & 63], out += '=';
  }
  return out;
}

std::string html_unescape(std::string s) {
  for (auto [from, to] : {std::pair{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&#x27;", "'"},
                          {"&nbsp;", " "}, {"&amp;", "&"}})
    s = replace_all(std::move(s), from, to);
  static const std::regex num("&#([0-9]+);");
  std::string out;
  auto begin = std::sregex_iterator(s.begin(), s.end(), num);
  size_t last = 0;
  for (auto it = begin; it != std::sregex_iterator(); ++it) {
    out += s.substr(last, it->position() - last);
    int code = std::stoi((*it)[1]);
    out += code < 128 ? std::string(1, char(code)) : it->str();
    last = it->position() + it->length();
  }
  return out + s.substr(last);
}

std::pair<std::vector<std::pair<std::string, std::string>>, std::string> front_matter(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> fields;
  if (!text.starts_with("---")) return {fields, std::string(text)};
  auto end = text.find("\n---", 3);
  if (end == std::string_view::npos) return {fields, std::string(text)};
  for (auto& line : lines(text.substr(3, end - 3))) {
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    auto value = trim(line.substr(colon + 1));
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
      value = value.substr(1, value.size() - 2);
    fields.emplace_back(trim(line.substr(0, colon)), value);
  }
  auto body_start = text.find('\n', end + 1);
  return {fields, body_start == std::string_view::npos ? "" : std::string(text.substr(body_start + 1))};
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
