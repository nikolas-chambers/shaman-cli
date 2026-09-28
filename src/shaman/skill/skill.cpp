#include "shaman/skill/skill.hpp"

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::skill {
namespace fs = std::filesystem;

namespace {

Skill parse(const std::string& text, const fs::path& dir, std::string source) {
  auto [fields, body] = str::front_matter(text);
  Skill s{dir.filename().string(), "", dir, str::trim(body), std::move(source)};
  for (auto& [k, v] : fields) {
    if (k == "name") s.name = v;
    if (k == "description") s.description = v;
  }
  return s;
}

// Unpack built-in skills once per content version so scripts are real files.
fs::path unpack_builtins() {
  std::string all;
  for (auto& f : embedded::skill_files()) all.append(f.skill).append(f.path).append(f.data);
  auto dir = paths::cache_dir() / "skills" / str::hash_hex(all);
  std::error_code ec;
  if (fs::exists(dir / ".complete", ec)) return dir;
  for (auto& f : embedded::skill_files()) {
    auto p = dir / f.skill / f.path;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary) << f.data;
    auto ext = p.extension().string();
    if (ext == ".py" || ext == ".sh" || ext == ".js")
      fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
  }
  std::ofstream(dir / ".complete") << "ok";
  log::debug(log::Cat::agent, "unpacked built-in skills to {}", dir.string());
  return dir;
}

void scan(const fs::path& dir, int depth, std::map<std::string, Skill>& found) {
  std::error_code ec;
  if (depth > 4 || !fs::is_directory(dir, ec)) return;
  if (auto file = dir / "SKILL.md"; fs::is_regular_file(file, ec)) {
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    auto s = parse(ss.str(), dir, file.string());
    log::debug(log::Cat::agent, "skill {} from {}", s.name, file.string());
    found[s.name] = std::move(s);
    return;  // a skill's own subfolders (scripts/, references/) are not skills
  }
  for (auto& e : fs::directory_iterator(dir, ec))
    if (e.is_directory(ec) && !e.path().filename().string().starts_with(".")) scan(e.path(), depth + 1, found);
}

}  // namespace

const std::vector<Skill>& discover(const fs::path& root) {
  static std::mutex mu;
  static std::map<fs::path, std::vector<Skill>> cache;
  std::lock_guard lock(mu);
  if (auto it = cache.find(root); it != cache.end()) return it->second;

  std::map<std::string, Skill> found;
  auto builtin = unpack_builtins();
  std::error_code ec;
  for (auto& e : fs::directory_iterator(builtin, ec)) {
    std::ifstream in(e.path() / "SKILL.md");
    if (!in) continue;
    std::stringstream ss;
    ss << in.rdbuf();
    auto s = parse(ss.str(), e.path(), "built-in");
    found[s.name] = std::move(s);
  }
  scan(paths::config_dir() / "skills", 0, found);
  scan(root / ".shaman" / "skills", 0, found);

  std::vector<Skill> out;
  for (auto& [_, s] : found) out.push_back(std::move(s));
  return cache[root] = std::move(out);
}

}  // namespace shaman::skill
