#include "shaman/tui/markdown.hpp"
#include "shaman/tui/theme.hpp"

#include <algorithm>
#include <cstdlib>

#include "shaman/core/strings.hpp"

namespace shaman::tui {

namespace style {
constexpr const char* reset = "\x1b[0m";
constexpr const char* bold = "\x1b[1m";
constexpr const char* italic = "\x1b[3m";
constexpr const char* dim = "\x1b[2m";
}  // namespace style

size_t display_width(std::string_view s) {
  size_t w = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = s[i];
    if (c == '\x1b') {
      while (i < s.size() && !((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) ++i;
      continue;
    }
    if ((c & 0xC0) != 0x80) ++w;
  }
  return w;
}

std::string clip(const std::string& s, size_t width) {
  std::string out;
  size_t w = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = s[i];
    if (c == '\x1b') {
      size_t j = i;
      while (j < s.size() && !((s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= 'a' && s[j] <= 'z'))) ++j;
      out += s.substr(i, j - i + 1);
      i = j;
      continue;
    }
    if ((c & 0xC0) != 0x80) {
      if (w == width) break;
      ++w;
    }
    out += char(c);
  }
  return out;
}

bool unicode() {
  static const bool yes = [] {
    for (auto var : {"LC_ALL", "LC_CTYPE", "LANG"})
      if (const char* v = std::getenv(var); v && *v) {
        auto s = str::lower(v);
        return s.find("utf-8") != std::string::npos || s.find("utf8") != std::string::npos;
      }
#ifdef _WIN32
    return true;  // console is switched to UTF-8 at startup
#else
    return false;
#endif
  }();
  return yes;
}

std::string glyph(const char* utf8, const char* ascii) { return unicode() ? utf8 : ascii; }

std::vector<std::string> wrap(const std::string& styled, size_t width, const std::string& indent) {
  std::vector<std::string> out;
  if (width < 8) width = 8;
  std::string line, word, active;  // `active` = SGR state to carry across breaks
  size_t line_w = 0, word_w = 0;
  auto flush_line = [&] {
    out.push_back(line + style::reset);
    line = indent + active;
    line_w = display_width(indent);
  };
  auto push_word = [&] {
    if (word.empty()) return;
    if (line_w + word_w > width && line_w > display_width(indent)) {
      while (!line.empty() && line.back() == ' ') line.pop_back(), --line_w;
      flush_line();
    }
    while (word_w > width) {  // hard-break very long words
      size_t take = 0, w = 0;
      while (take < word.size() && w < width - line_w) {
        unsigned char c = word[take];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        take += len, ++w;
      }
      line += word.substr(0, take);
      word.erase(0, take);
      word_w -= w;
      flush_line();
    }
    line += word;
    line_w += word_w;
    word.clear();
    word_w = 0;
  };
  for (size_t i = 0; i < styled.size(); ++i) {
    char c = styled[i];
    if (c == '\x1b') {
      size_t j = i;
      while (j < styled.size() && styled[j] != 'm') ++j;
      auto seq = styled.substr(i, j - i + 1);
      word += seq;
      active = seq == style::reset ? "" : active + seq;
      i = j;
      continue;
    }
    if (c == ' ') {
      push_word();
      if (line_w < width) line += ' ', ++line_w;
      continue;
    }
    word += c;
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++word_w;
  }
  push_word();
  out.push_back(line + style::reset);
  return out;
}

static std::string inline_md(const std::string& s) {
  std::string out;
  bool b = false, it = false, code = false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '`') {
      code = !code;
      out += code ? theme().code : std::string(style::reset) + (b ? style::bold : "") + (it ? style::italic : "");
      continue;
    }
    if (!code && s.compare(i, 2, "**") == 0) {
      b = !b;
      out += b ? style::bold : std::string(style::reset) + (it ? style::italic : "");
      ++i;
      continue;
    }
    if (!code && s[i] == '*' && i + 1 < s.size() && s[i + 1] != ' ' && (i == 0 || s[i - 1] == ' ' || it)) {
      it = !it;
      out += it ? style::italic : std::string(style::reset) + (b ? style::bold : "");
      continue;
    }
    out += s[i];
  }
  return out + style::reset;
}

static std::string inline_md(const std::string& s);

// Render a block of "| a | b |" lines as an aligned table (falls back to wrapping when too wide).
static std::vector<std::string> render_table(const std::vector<std::string>& block, size_t width) {
  std::vector<std::vector<std::string>> rows;
  for (auto& l : block) {
    auto t = str::trim(l);
    if (t.front() == '|') t.erase(0, 1);
    if (!t.empty() && t.back() == '|') t.pop_back();
    auto cells = str::split(t, '|');
    for (auto& c : cells) c = str::trim(c);
    bool separator = std::ranges::all_of(cells, [](const std::string& c) {
      return !c.empty() && c.find_first_not_of(":-") == std::string::npos;
    });
    if (!separator) rows.push_back(cells);
  }
  size_t cols = 0;
  for (auto& r : rows) cols = std::max(cols, r.size());
  std::vector<size_t> w(cols, 0);
  for (auto& r : rows)
    for (size_t i = 0; i < r.size(); ++i) w[i] = std::max(w[i], display_width(r[i]));
  size_t total = 1;
  for (auto x : w) total += x + 3;
  std::vector<std::string> out;
  if (total > width) {  // too wide: one wrapped line per row
    for (auto& r : rows)
      for (auto& l : wrap(str::join(r, " | "), width)) out.push_back(l);
    return out;
  }
  auto bar = [&](const char* l, const char* m, const char* r) {
    std::string s = std::string(style::dim) + glyph(l, "+");
    for (size_t i = 0; i < cols; ++i) {
      for (size_t k = 0; k < w[i] + 2; ++k) s += glyph("─", "-");
      s += i + 1 < cols ? glyph(m, "+") : glyph(r, "+");
    }
    return s + style::reset;
  };
  out.push_back(bar("┌", "┬", "┐"));
  for (size_t r = 0; r < rows.size(); ++r) {
    std::string s = std::string(style::dim) + glyph("│", "|") + style::reset;
    for (size_t i = 0; i < cols; ++i) {
      auto cell = i < rows[r].size() ? rows[r][i] : "";
      auto pad = std::string(w[i] - display_width(cell), ' ');
      s += " " + (r == 0 ? std::string(style::bold) + cell + style::reset : inline_md(cell)) + pad + " " + style::dim + glyph("│", "|") + style::reset;
    }
    out.push_back(s);
    if (r == 0 && rows.size() > 1) out.push_back(bar("├", "┼", "┤"));
  }
  out.push_back(bar("└", "┴", "┘"));
  return out;
}

std::vector<std::string> render_markdown(const std::string& text, size_t width) {
  std::vector<std::string> out;
  bool fence = false;
  std::vector<std::string> table;
  auto flush_table = [&] {
    if (table.empty()) return;
    auto t = render_table(table, width);
    out.insert(out.end(), t.begin(), t.end());
    table.clear();
  };
  for (auto& raw : str::split(text, '\n')) {
    if (!fence && str::trim(raw).starts_with("|")) {
      table.push_back(raw);
      continue;
    }
    flush_table();
    auto line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto t = str::trim(line);
    if (t.starts_with("```")) {
      fence = !fence;
      out.push_back(std::string(style::dim) + (fence ? glyph("┌ ", "+ ") + t.substr(3) : glyph("└", "+")) + style::reset);
      continue;
    }
    if (fence) {
      for (auto& l : wrap(std::string(theme().code) + line, width - 2)) out.push_back(std::string(style::dim) + glyph("│ ", "| ") + style::reset + l);
      continue;
    }
    if (t.starts_with("#")) {
      auto h = t.substr(t.find_first_not_of('#'));
      for (auto& l : wrap(std::string(theme().heading) + str::trim(h), width)) out.push_back(l);
      continue;
    }
    if (t.starts_with("> ")) {
      for (auto& l : wrap(std::string(style::dim) + inline_md(t.substr(2)), width - 2)) out.push_back(std::string(style::dim) + glyph("▎ ", "> ") + l);
      continue;
    }
    size_t indent = line.find_first_not_of(' ');
    if (indent == std::string::npos) indent = 0;
    if (t.starts_with("- ") || t.starts_with("* ") || t.starts_with("+ ")) {
      auto pad = std::string(indent, ' ');
      auto ls = wrap(pad + glyph("• ", "* ") + inline_md(t.substr(2)), width, pad + "  ");
      out.insert(out.end(), ls.begin(), ls.end());
      continue;
    }
    if (t == "---" || t == "***") {
      out.push_back(std::string(style::dim) + std::string(std::min<size_t>(width, 40), '-') + style::reset);
      continue;
    }
    auto ls = wrap(inline_md(line), width, std::string(indent, ' '));
    out.insert(out.end(), ls.begin(), ls.end());
  }
  flush_table();
  return out;
}

}  // namespace shaman::tui
