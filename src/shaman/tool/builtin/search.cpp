// list, glob, grep. ripgrep is used when installed (fast, respects .gitignore);
// otherwise a portable std::filesystem walk takes over.
#include <algorithm>
#include <format>
#include <regex>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/tool/builtin/common.hpp"

namespace shaman::tool::detail {
namespace {

// Walk `dir`, calling fn(path) for regular files; stops when fn returns false.
template <class Fn>
void walk(const fs::path& dir, Fn&& fn) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->is_directory(ec) && skip_dir(it->path())) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec) && !fn(it->path())) break;
  }
}

std::vector<fs::path> rg_files(const fs::path& dir) {
  std::vector<fs::path> out;
  auto res = process::run({"rg", "--files", "--hidden", "--glob", "!.git", dir.string()}, {.cwd = dir});
  if (!res) return out;
  for (auto& l : str::lines(res->output)) out.push_back(fs::path(l).is_absolute() ? fs::path(l) : dir / l);
  return out;
}

class List final : public Tool {
 public:
  std::string name() const override { return "list"; }
  std::string description() const override {
    return "List files and directories as a tree. Skips .git, node_modules and similar.";
  }
  Json schema() const override {
    return {{"type", "object"}, {"properties", {{"path", {{"type", "string"}, {"description", "Directory (default: project root)"}}}}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto dir = ctx.path(in.value("path", ctx.root.string()));
    if (!dir) return error(dir.error().message);
    if (!ctx.permit("list", dir->string(), "List " + rel(ctx, *dir))) return error("permission denied");
    std::vector<std::string> entries;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(*dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && entries.size() < 500; it.increment(ec)) {
      if (ec) break;
      bool is_dir = it->is_directory(ec);
      if (is_dir && skip_dir(it->path())) {
        it.disable_recursion_pending();
        continue;
      }
      entries.push_back(std::string(it.depth() * 2, ' ') + it->path().filename().string() + (is_dir ? "/" : ""));
    }
    auto out = rel(ctx, *dir) + "/\n" + str::join(entries, "\n");
    if (entries.size() >= 500) out += "\n(truncated at 500 entries)";
    return {out, false, "List " + rel(ctx, *dir)};
  }
};

class Glob final : public Tool {
 public:
  std::string name() const override { return "glob"; }
  std::string description() const override {
    return "Find files by glob pattern such as \"**/*.cpp\" or \"src/**/test_*.py\". Newest first.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"pattern", {{"type", "string"}}}, {"path", {{"type", "string"}}}}},
            {"required", {"pattern"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto dir = ctx.path(in.value("path", ctx.root.string()));
    if (!dir) return error(dir.error().message);
    auto pattern = in.value("pattern", "");
    if (!ctx.permit("glob", pattern, "Glob " + pattern)) return error("permission denied");
    std::vector<fs::path> files = process::which("rg") ? rg_files(*dir) : std::vector<fs::path>{};
    if (files.empty()) walk(*dir, [&](const fs::path& p) { files.push_back(p); return files.size() < 100'000; });

    std::vector<std::pair<fs::file_time_type, fs::path>> hits;
    std::error_code ec;
    for (auto& f : files)
      if (str::glob(pattern, f.lexically_relative(*dir).generic_string()))
        hits.emplace_back(fs::last_write_time(f, ec), f);
    std::ranges::sort(hits, std::greater{}, &std::pair<fs::file_time_type, fs::path>::first);
    std::vector<std::string> out;
    for (size_t i = 0; i < hits.size() && i < 100; ++i) out.push_back(rel(ctx, hits[i].second));
    if (out.empty()) return {"No files found", false, "Glob " + pattern};
    if (hits.size() > 100) out.push_back(std::format("({} more; refine the pattern)", hits.size() - 100));
    return {str::join(out, "\n"), false, "Glob " + pattern};
  }
};

class Grep final : public Tool {
 public:
  std::string name() const override { return "grep"; }
  std::string description() const override {
    return "Search file contents with a regular expression. Optional include glob filters files "
           "(e.g. \"*.hpp\"). Returns path:line: text.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"pattern", {{"type", "string"}}}, {"path", {{"type", "string"}}},
                            {"include", {{"type", "string"}}}}},
            {"required", {"pattern"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto dir = ctx.path(in.value("path", ctx.root.string()));
    if (!dir) return error(dir.error().message);
    auto pattern = in.value("pattern", ""), include = in.value("include", "");
    if (!ctx.permit("grep", pattern, "Grep " + pattern)) return error("permission denied");

    std::vector<std::string> hits;
    if (process::which("rg")) {
      std::vector<std::string> argv{"rg", "-n", "--no-heading", "--color", "never", "-H", "--max-columns", "300"};
      if (!include.empty()) argv.insert(argv.end(), {"--glob", include});
      argv.insert(argv.end(), {"-e", pattern, dir->string()});
      auto res = process::run(argv, {.cwd = *dir, .cancel = ctx.cancel});
      if (!res) return error(res.error().message);
      if (res->exit_code == 2) return error("rg: " + res->output);
      for (auto& l : str::lines(res->output)) {
        hits.push_back(l.starts_with(ctx.root.string() + "/") ? l.substr(ctx.root.string().size() + 1) : l);
        if (hits.size() > 200) break;
      }
    } else {
      std::regex re;
      try {
        re = std::regex(pattern);
      } catch (const std::regex_error& e) {
        return error(std::string("invalid regex: ") + e.what());
      }
      walk(*dir, [&](const fs::path& p) {
        if (!include.empty() && !str::glob(include, p.lexically_relative(*dir).generic_string())) return true;
        auto content = read_all(p);
        if (!content || content->substr(0, 4096).find('\0') != std::string::npos) return true;
        auto ls = str::lines(*content);
        for (size_t i = 0; i < ls.size(); ++i)
          if (std::regex_search(ls[i], re)) hits.push_back(std::format("{}:{}:{}", rel(ctx, p), i + 1, ls[i].substr(0, 300)));
        return hits.size() <= 200;
      });
    }
    if (hits.empty()) return {"No matches", false, "Grep " + pattern};
    return {truncate(str::join(hits, "\n")), false, std::format("Grep {} ({} matches)", pattern, hits.size())};
  }
};

}  // namespace

std::unique_ptr<Tool> make_list() { return std::make_unique<List>(); }
std::unique_ptr<Tool> make_glob() { return std::make_unique<Glob>(); }
std::unique_ptr<Tool> make_grep() { return std::make_unique<Grep>(); }

}  // namespace shaman::tool::detail
