#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "shaman/tool/tool.hpp"

namespace shaman::tool::detail {

inline std::optional<std::string> read_all(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

inline bool write_all(const fs::path& p, const std::string& content) {
  std::error_code ec;
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << content;
  return bool(out);
}

inline std::string rel(const Context& ctx, const fs::path& p) {
  auto r = p.lexically_relative(ctx.root);
  return r.empty() || *r.begin() == ".." ? p.string() : r.generic_string();
}

inline bool skip_dir(const fs::path& p) {
  auto n = p.filename().string();
  return n == ".git" || n == "node_modules" || n == ".cache" || n == "__pycache__" || n == ".venv";
}

std::unique_ptr<Tool> make_read();
std::unique_ptr<Tool> make_write();
std::unique_ptr<Tool> make_edit();
std::unique_ptr<Tool> make_list();
std::unique_ptr<Tool> make_glob();
std::unique_ptr<Tool> make_grep();
std::unique_ptr<Tool> make_bash();
std::unique_ptr<Tool> make_webfetch();
std::unique_ptr<Tool> make_todowrite();
std::unique_ptr<Tool> make_todoread();
std::unique_ptr<Tool> make_task();

}  // namespace shaman::tool::detail
