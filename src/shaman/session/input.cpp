#include "shaman/session/input.hpp"

#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::session {
namespace fs = std::filesystem;

std::string media_type(const fs::path& p) {
  auto ext = str::lower(p.extension().string());
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".webp") return "image/webp";
  return "";
}

Composed compose(const std::string& text, const std::vector<std::string>& attachments, const fs::path& root) {
  Composed out;
  out.message.role = llm::Role::user;
  std::string body = text;

  std::vector<fs::path> paths_to_add;
  std::set<fs::path> seen;
  static const std::regex mention(R"((^|\s)@([^\s]+))");
  for (auto it = std::sregex_iterator(text.begin(), text.end(), mention); it != std::sregex_iterator(); ++it) {
    std::string raw = (*it)[2];
    while (!raw.empty() && std::string_view(".,;:!?)").find(raw.back()) != std::string_view::npos) raw.pop_back();
    auto p = paths::resolve(root, raw);
    std::error_code ec;
    if (fs::exists(p, ec) && seen.insert(p).second) paths_to_add.push_back(p);
  }
  for (auto& a : attachments) {
    auto p = paths::resolve(root, a);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
      out.warnings.push_back("attachment not found: " + a);
      continue;
    }
    if (seen.insert(p).second) paths_to_add.push_back(p);
  }

  for (auto& p : paths_to_add) {
    auto rel = p.lexically_relative(root).generic_string();
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
      std::string listing;
      int n = 0;
      for (auto& e : fs::directory_iterator(p, ec)) {
        if (++n > 200) break;
        listing += e.path().filename().string() + (e.is_directory(ec) ? "/" : "") + "\n";
      }
      body += std::format("\n\n<directory path=\"{}\">\n{}</directory>", rel, listing);
      continue;
    }
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    auto data = ss.str();
    if (auto mt = media_type(p); !mt.empty()) {
      if (data.size() > 5 * 1024 * 1024) {
        out.warnings.push_back(rel + " is over 5 MB; not attached");
        continue;
      }
      out.message.parts.push_back(llm::ImagePart{mt, str::base64_encode(data)});
      continue;
    }
    if (data.substr(0, 8192).find('\0') != std::string::npos) {
      out.warnings.push_back(rel + " is binary; not attached");
      continue;
    }
    if (data.size() > 256 * 1024) {
      data.resize(256 * 1024);
      out.warnings.push_back(rel + " truncated to 256 KB");
    }
    body += std::format("\n\n<file path=\"{}\">\n{}\n</file>", rel, data);
    out.files.push_back(p);
  }
  out.message.parts.insert(out.message.parts.begin(), llm::TextPart{body});
  return out;
}

}  // namespace shaman::session
