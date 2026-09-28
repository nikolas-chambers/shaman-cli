#include "shaman/session/snapshot.hpp"

#include <format>

#include "shaman/core/log.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::session {

Snapshot::Snapshot(std::filesystem::path worktree, std::filesystem::path git_dir)
    : worktree_(std::move(worktree)), git_dir_(std::move(git_dir)) {}

Result<std::string> Snapshot::git(const std::string& args) {
  auto cmd = std::format("git --git-dir \"{}\" --work-tree \"{}\" {}", git_dir_.string(), worktree_.string(), args);
  auto res = process::shell(cmd, {.cwd = worktree_, .timeout = std::chrono::seconds(60)});
  if (!res) return std::unexpected(res.error());
  if (res->exit_code != 0) return fail("git " + args + ": " + str::trim(res->output));
  return str::trim(res->output);
}

std::optional<std::string> Snapshot::track() {
  if (!process::which("git")) return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::exists(git_dir_ / "HEAD", ec)) {
    std::filesystem::create_directories(git_dir_, ec);
    if (!git("init -q")) return std::nullopt;
  }
  if (!git("add -A .")) return std::nullopt;
  auto tree = git("write-tree");
  if (!tree) {
    log::debug(log::Cat::session, "snapshot failed: {}", tree.error().message);
    return std::nullopt;
  }
  log::debug(log::Cat::session, "snapshot {}", *tree);
  return *tree;
}

Result<void> Snapshot::restore(const std::string& tree) {
  if (auto r = git("read-tree " + tree); !r) return std::unexpected(r.error());
  if (auto r = git("checkout-index -a -f"); !r) return std::unexpected(r.error());
  return {};
}

std::string Snapshot::diff(const std::string& tree) {
  git("add -A .");
  return git("diff --cached --stat " + tree).value_or("");
}

}  // namespace shaman::session
