#include "shaman/index/index.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::index {

namespace fs = std::filesystem;

namespace {

struct Pattern {
  std::regex re;
  int group;         // capture group holding the name
  const char* kind;
  const char* hint;  // cheap substring prefilter; nullptr = none
};

using Patterns = std::vector<Pattern>;

Pattern P(const char* re, int group, const char* kind, const char* hint = nullptr) {
  return {std::regex(re, std::regex::optimize), group, kind, hint};
}

const Patterns* patterns_for(const std::string& ext) {
  static const Patterns cpp = {
      P(R"(^\s*(?:template\s*<[^>]*>\s*)?(class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?(\w+)\s*(?:final\s*)?[:{])", 2, "class"),
      P(R"(^\s*enum\s+(?:class\s+|struct\s+)?(\w+))", 1, "enum", "enum"),
      P(R"(^\s*namespace\s+([\w:]+)\s*\{)", 1, "namespace", "namespace"),
      P(R"(^[\w:<>,\*&\s~]*?\b([A-Za-z_][\w:~]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:->\s*[\w:<>]+\s*)?\{\s*$)", 1, "function", "("),
      P(R"(^\s*(?:using|typedef)\s+(\w+)\s*=)", 1, "type", "using")};
  static const Patterns py = {P(R"(^\s*(?:async\s+)?def\s+(\w+))", 1, "function", "def"),
                              P(R"(^\s*class\s+(\w+))", 1, "class", "class")};
  static const Patterns js = {
      P(R"(^\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s+(\w+))", 1, "function", "function"),
      P(R"(^\s*(?:export\s+)?(?:default\s+)?(?:abstract\s+)?class\s+(\w+))", 1, "class", "class"),
      P(R"(^\s*(?:export\s+)?(?:const|let|var)\s+(\w+)\s*=\s*(?:async\s*)?(?:\([^)]*\)|\w+)\s*=>)", 1, "function", "=>"),
      P(R"(^\s*(?:export\s+)?(?:interface|type)\s+(\w+))", 1, "type"),
      P(R"(^\s*(?:export\s+)?enum\s+(\w+))", 1, "enum", "enum")};
  static const Patterns go = {P(R"(^func\s+(?:\([^)]*\)\s*)?(\w+))", 1, "function", "func"),
                              P(R"(^type\s+(\w+))", 1, "type", "type")};
  static const Patterns rust = {P(R"(^\s*(?:pub(?:\([^)]*\))?\s+)?(?:async\s+)?(?:unsafe\s+)?fn\s+(\w+))", 1, "function", "fn"),
                                P(R"(^\s*(?:pub(?:\([^)]*\))?\s+)?(struct|enum|trait|union)\s+(\w+))", 2, "type"),
                                P(R"(^\s*impl(?:<[^>]*>)?\s+(?:[\w:<>]+\s+for\s+)?(\w+))", 1, "impl", "impl"),
                                P(R"(^\s*(?:pub\s+)?mod\s+(\w+))", 1, "module", "mod")};
  static const Patterns jvm = {  // Java, Kotlin, C#, Scala, Dart
      P(R"(^\s*(?:(?:public|private|protected|internal|static|final|abstract|sealed|open|data|partial)\s+)*(class|interface|enum|record|object|struct)\s+(\w+))", 2, "class"),
      P(R"(^\s*(?:(?:public|private|protected|internal|static|final|abstract|override|virtual|async|synchronized)\s+)+[\w<>\[\],\s]+\s+(\w+)\s*\([^;]*\)\s*(?:throws [\w,\s]+)?\{?\s*$)", 1, "method", "("),
      P(R"(^\s*(?:suspend\s+)?fun\s+(?:<[^>]*>\s*)?(?:[\w.]+\.)?(\w+))", 1, "function", "fun")};
  static const Patterns ruby = {P(R"(^\s*def\s+(?:self\.)?(\w+[?!=]?))", 1, "method", "def"),
                                P(R"(^\s*(class|module)\s+([\w:]+))", 2, "class")};
  static const Patterns php = {P(R"(^\s*(?:(?:public|private|protected|static|abstract|final)\s+)*function\s+(\w+))", 1, "function", "function"),
                               P(R"(^\s*(?:abstract\s+|final\s+)?(class|interface|trait|enum)\s+(\w+))", 2, "class")};
  static const Patterns swift = {P(R"(^\s*(?:(?:public|private|internal|fileprivate|open|static|final|override|mutating)\s+)*func\s+(\w+))", 1, "function", "func"),
                                 P(R"(^\s*(?:(?:public|private|internal|open|final)\s+)*(class|struct|enum|protocol|extension|actor)\s+(\w+))", 2, "type")};
  static const Patterns shell = {P(R"(^\s*(?:function\s+)?([\w-]+)\s*\(\)\s*\{)", 1, "function", "()")};
  static const Patterns lua = {P(R"(^\s*(?:local\s+)?function\s+([\w.:]+))", 1, "function", "function")};
  static const Patterns zig = {P(R"(^\s*(?:pub\s+)?fn\s+(\w+))", 1, "function", "fn"),
                               P(R"(^\s*(?:pub\s+)?const\s+(\w+)\s*=\s*(?:struct|enum|union))", 1, "type", "const")};
  static const std::map<std::string, const Patterns*> by_ext = {
      {".c", &cpp}, {".h", &cpp}, {".cc", &cpp}, {".cpp", &cpp}, {".cxx", &cpp}, {".hpp", &cpp}, {".hh", &cpp}, {".hxx", &cpp},
      {".m", &cpp}, {".mm", &cpp}, {".py", &py}, {".pyi", &py}, {".js", &js}, {".jsx", &js}, {".mjs", &js}, {".cjs", &js},
      {".ts", &js}, {".tsx", &js}, {".mts", &js}, {".vue", &js}, {".svelte", &js}, {".go", &go}, {".rs", &rust},
      {".java", &jvm}, {".kt", &jvm}, {".kts", &jvm}, {".cs", &jvm}, {".scala", &jvm}, {".dart", &jvm}, {".rb", &ruby},
      {".php", &php}, {".swift", &swift}, {".sh", &shell}, {".bash", &shell}, {".zsh", &shell}, {".lua", &lua}, {".zig", &zig}};
  auto it = by_ext.find(str::lower(ext));
  return it == by_ext.end() ? nullptr : it->second;
}

bool keyword(const std::string& name) {
  static const std::set<std::string> kw = {"if", "for", "while", "switch", "return", "catch", "else", "do", "sizeof",
                                           "new", "delete", "throw", "case", "static_assert", "decltype", "defined"};
  return kw.contains(name);
}

}  // namespace

std::vector<Symbol> scan(const std::string& path, const std::string& text) {
  std::vector<Symbol> out;
  auto* pats = patterns_for(fs::path(path).extension().string());
  if (!pats) return out;
  std::istringstream in(text);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ++n;
    if (line.size() > 400 || line.empty()) continue;
    for (auto& p : *pats) {
      if (p.hint && line.find(p.hint) == std::string::npos) continue;
      std::smatch m;
      if (!std::regex_search(line, m, p.re)) continue;
      auto name = m[size_t(p.group)].str();
      if (name.empty() || keyword(name)) continue;
      std::string kind = p.kind;
      if (kind == "class" && m.size() > 2 && p.group == 2) kind = m[1].str();  // class / struct / interface ...
      if (kind == "type" && m.size() > 2 && p.group == 2) kind = m[1].str();
      out.push_back({name, kind, path, n});
      break;
    }
  }
  return out;
}

Index::Index(fs::path root, fs::path cache_file) : root_(std::move(root)), cache_(std::move(cache_file)) { load(); }

std::vector<std::string> Index::list_files() const {
  std::vector<std::string> out;
  if (process::which("git")) {
    auto r = process::run({"git", "ls-files", "--cached", "--others", "--exclude-standard"},
                          {.cwd = root_, .timeout = std::chrono::seconds(20), .max_output = 64u << 20});
    if (r && r->exit_code == 0) {
      for (auto& l : str::lines(r->output))
        if (!l.empty()) out.push_back(l);
      return out;
    }
  }
  static const std::set<std::string> skip = {".git", "node_modules", "build", "dist", "target", "vendor", "__pycache__",
                                             ".venv", "venv", ".next", "out", ".cache", ".idea", ".vscode", "bin", "obj"};
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root_, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->is_directory(ec) && (skip.contains(it->path().filename().string()) || it->path().filename().string().starts_with("build-"))) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec)) out.push_back(fs::relative(it->path(), root_, ec).generic_string());
    if (out.size() > 50'000) break;
  }
  return out;
}

void Index::refresh() {
  auto files = list_files();
  std::set<std::string> present;
  size_t scanned = 0;
  for (auto& rel : files) {
    if (!patterns_for(fs::path(rel).extension().string())) continue;
    present.insert(rel);
    std::error_code ec;
    auto full = root_ / rel;
    auto size = fs::file_size(full, ec);
    if (ec || size > (1u << 20)) continue;  // skip huge or generated files
    auto mtime = int64_t(fs::last_write_time(full, ec).time_since_epoch().count());
    auto& entry = files_[rel];
    if (entry.mtime == mtime && mtime != 0) continue;
    std::ifstream in(full, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    entry = {mtime, scan(rel, text)};
    ++scanned;
  }
  std::erase_if(files_, [&](auto& kv) { return !present.contains(kv.first); });
  if (scanned) save();
  log::debug(log::Cat::tool, "index: {} files ({} rescanned), {} symbols", files_.size(), scanned, symbol_count());
}

size_t Index::symbol_count() const {
  size_t n = 0;
  for (auto& [_, f] : files_) n += f.symbols.size();
  return n;
}

std::vector<Symbol> Index::find(const std::string& query, size_t limit) const {
  auto q = str::lower(query);
  std::vector<Symbol> exact, partial;
  for (auto& [_, f] : files_)
    for (auto& s : f.symbols) {
      auto n = str::lower(s.name);
      if (n == q || n.ends_with("::" + q)) exact.push_back(s);
      else if (n.find(q) != std::string::npos) partial.push_back(s);
    }
  exact.insert(exact.end(), partial.begin(), partial.end());
  if (exact.size() > limit) exact.resize(limit);
  return exact;
}

std::vector<Symbol> Index::outline(const std::string& file) const {
  auto it = files_.find(fs::path(file).generic_string());
  return it == files_.end() ? std::vector<Symbol>{} : it->second.symbols;
}

std::string Index::map(const std::string& dir_filter, size_t max_chars) const {
  auto prefix = fs::path(dir_filter).generic_string();
  while (prefix.starts_with("./")) prefix.erase(0, 2);
  if (prefix == ".") prefix.clear();
  if (!prefix.empty() && !prefix.ends_with("/")) prefix += "/";
  // group files by directory; list the most important symbols (types first) per file
  std::map<std::string, std::vector<const std::pair<const std::string, FileEntry>*>> dirs;
  for (auto& kv : files_)
    if (prefix.empty() || kv.first.starts_with(prefix)) dirs[fs::path(kv.first).parent_path().generic_string()].push_back(&kv);
  std::string out;
  for (auto& [dir, list] : dirs) {
    std::string block = (dir.empty() ? "." : dir) + "/\n";
    for (auto* kv : list) {
      std::vector<std::string> names;
      for (auto& s : kv->second.symbols)
        if (s.kind != "function" && s.kind != "method") names.push_back(s.name);
      for (auto& s : kv->second.symbols)
        if ((s.kind == "function" || s.kind == "method") && names.size() < 8) names.push_back(s.name + "()");
      if (names.size() > 8) names.resize(8);
      block += "  " + fs::path(kv->first).filename().string() + (names.empty() ? "" : ": " + str::join(names, ", ")) + "\n";
    }
    if (out.size() + block.size() > max_chars) {
      out += "... (" + std::to_string(files_.size()) + " files in total; ask for a directory or use find)\n";
      break;
    }
    out += block;
  }
  return out;
}

void Index::load() {
  std::ifstream in(cache_);
  if (!in) return;
  try {
    auto j = Json::parse(in);
    for (auto& [file, e] : j.value("files", Json::object()).items()) {
      FileEntry fe{e.value("mtime", int64_t(0)), {}};
      for (auto& s : e.value("symbols", Json::array()))
        fe.symbols.push_back({s.value("n", ""), s.value("k", ""), file, s.value("l", 0)});
      files_[file] = std::move(fe);
    }
  } catch (...) {
    files_.clear();
  }
}

void Index::save() const {
  Json files = Json::object();
  for (auto& [file, e] : files_) {
    Json syms = Json::array();
    for (auto& s : e.symbols) syms.push_back({{"n", s.name}, {"k", s.kind}, {"l", s.line}});
    files[file] = {{"mtime", e.mtime}, {"symbols", syms}};
  }
  std::error_code ec;
  fs::create_directories(cache_.parent_path(), ec);
  std::ofstream(cache_) << Json{{"version", 1}, {"files", files}}.dump();
}

}  // namespace shaman::index
