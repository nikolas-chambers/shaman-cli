#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"

// A fast symbol index of the project: definitions (functions, classes, types, ...) found with per-language
// patterns, so the model can jump to code in a large repo instead of grepping blindly. Files come from
// `git ls-files` when available (so .gitignore is respected), otherwise a walk that skips vendored and build
// directories. The index is cached per project and refreshed incrementally by file modification time.
namespace shaman::index {

struct Symbol {
  std::string name, kind;  // kind: function, class, struct, type, method, ...
  std::string file;        // relative to the project root
  int line = 0;
};

class Index {
 public:
  Index(std::filesystem::path root, std::filesystem::path cache_file);
  void refresh();  // rescan changed files
  // Definitions whose name matches exactly (case-insensitive), then those containing `query`; at most `limit`.
  std::vector<Symbol> find(const std::string& query, size_t limit = 50) const;
  std::vector<Symbol> outline(const std::string& file) const;
  // A compact map: directories, their files and main symbols, within about `max_chars`.
  std::string map(const std::string& dir = "", size_t max_chars = 6000) const;  // dir: only under this directory
  size_t file_count() const { return files_.size(); }
  size_t symbol_count() const;

 private:
  struct FileEntry {
    int64_t mtime = 0;
    std::vector<Symbol> symbols;
  };
  std::vector<std::string> list_files() const;
  void load();
  void save() const;
  std::filesystem::path root_, cache_;
  std::map<std::string, FileEntry> files_;  // by relative path
};

// Definitions in one file's text; language from the extension. Exposed for tests.
std::vector<Symbol> scan(const std::string& path, const std::string& text);

}  // namespace shaman::index
