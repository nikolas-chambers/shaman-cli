#include "shaman/session/format.hpp"

#include <fstream>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::session {
namespace fs = std::filesystem;

namespace {

struct Formatter {
  std::string name;
  std::vector<std::string> command;  // $FILE is replaced
  std::vector<std::string> extensions;
  std::vector<std::string> markers;  // any of these in the project root enables it
  std::string marker_text;           // ...or this text inside pyproject.toml / package.json
};

bool contains(const fs::path& p, const std::string& needle) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str().find(needle) != std::string::npos;
}

std::vector<Formatter> builtins() {
  return {
      {"clang-format", {"clang-format", "-i", "$FILE"}, {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}, {".clang-format", "_clang-format"}, ""},
      {"prettier", {"npx", "--no-install", "prettier", "--write", "$FILE"},
       {".js", ".jsx", ".ts", ".tsx", ".mjs", ".cjs", ".json", ".css", ".scss", ".html", ".md", ".yaml", ".yml", ".vue", ".svelte"},
       {".prettierrc", ".prettierrc.json", ".prettierrc.js", ".prettierrc.yaml", "prettier.config.js", "prettier.config.mjs"}, "\"prettier\""},
      {"ruff", {"ruff", "format", "$FILE"}, {".py", ".pyi"}, {"ruff.toml", ".ruff.toml"}, "[tool.ruff"},
      {"black", {"black", "-q", "$FILE"}, {".py", ".pyi"}, {}, "[tool.black"},
      {"gofmt", {"gofmt", "-w", "$FILE"}, {".go"}, {"go.mod"}, ""},
      {"rustfmt", {"rustfmt", "$FILE"}, {".rs"}, {"Cargo.toml"}, ""},
      {"zig", {"zig", "fmt", "$FILE"}, {".zig"}, {"build.zig"}, ""},
  };
}

bool enabled(const Formatter& f, const fs::path& root) {
  std::error_code ec;
  for (auto& m : f.markers)
    if (fs::exists(root / m, ec)) return true;
  if (!f.marker_text.empty())
    for (auto file : {"pyproject.toml", "package.json"})
      if (fs::exists(root / file, ec) && contains(root / file, f.marker_text)) return true;
  return false;
}

}  // namespace

std::string format_file(const Config& config, const fs::path& root, const fs::path& file) {
  auto cfg = config.raw.value("formatter", Json::object());
  if (cfg.is_boolean() && !cfg.get<bool>()) return "";
  auto ext = file.extension().string();

  std::vector<Formatter> candidates;
  if (cfg.is_object())
    for (auto& [name, spec] : cfg.items())
      if (spec.is_object() && spec.contains("command"))
        candidates.push_back({name, spec["command"].get<std::vector<std::string>>(),
                              spec.value("extensions", std::vector<std::string>{}), {}, ""});
  for (auto& f : builtins()) {
    if (cfg.is_object() && cfg.contains(f.name) && cfg[f.name].is_boolean() && !cfg[f.name].get<bool>()) continue;
    if (enabled(f, root)) candidates.push_back(f);
  }
  for (auto& f : candidates) {
    if (std::ranges::find(f.extensions, ext) == f.extensions.end()) continue;
    if (!process::which(f.command.front())) continue;
    auto argv = f.command;
    for (auto& a : argv) a = str::replace_all(a, "$FILE", file.string());
    auto r = process::run(argv, {.cwd = root, .timeout = std::chrono::seconds(20)});
    bool ok = r && r->exit_code == 0;
    log::debug(log::Cat::tool, "formatter {} on {}: {}", f.name, file.string(), ok ? "ok" : r ? r->output : r.error().message);
    return ok ? f.name : "";
  }
  return "";
}

}  // namespace shaman::session
