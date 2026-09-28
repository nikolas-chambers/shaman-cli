#include "shaman/extras/worktree.hpp"

#include <regex>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::extras {
namespace fs = std::filesystem;

static Result<std::string> git(const fs::path& root, const std::vector<std::string>& args) {
  std::vector<std::string> argv{"git"};
  argv.insert(argv.end(), args.begin(), args.end());
  auto r = process::run(argv, {.cwd = root, .timeout = std::chrono::minutes(2)});
  if (!r) return std::unexpected(r.error());
  if (r->exit_code != 0) return fail(str::trim(r->output));
  return str::trim(r->output);
}

static fs::path path_for(const fs::path& root, const std::string& name) {
  return root.parent_path() / (root.filename().string() + "-" + name);
}

Result<fs::path> worktree_create(const fs::path& root, const std::string& name) {
  if (!std::regex_match(name, std::regex(R"([A-Za-z0-9._/-]+)"))) return fail("worktree names use letters, digits, . _ / -");
  auto safe = str::replace_all(name, "/", "-");
  auto dir = path_for(root, safe);
  std::error_code ec;
  if (fs::exists(dir, ec)) return dir;  // reopen
  auto branch = "shaman/" + name;
  auto exists = git(root, {"rev-parse", "--verify", "--quiet", branch});
  auto r = exists ? git(root, {"worktree", "add", dir.string(), branch}) : git(root, {"worktree", "add", "-b", branch, dir.string()});
  if (!r) return std::unexpected(r.error());
  return dir;
}

Result<std::string> worktree_list(const fs::path& root) { return git(root, {"worktree", "list"}); }

Result<void> worktree_remove(const fs::path& root, const std::string& name) {
  auto r = git(root, {"worktree", "remove", path_for(root, str::replace_all(name, "/", "-")).string()});
  if (!r) return std::unexpected(r.error());
  return {};
}

}  // namespace shaman::extras
