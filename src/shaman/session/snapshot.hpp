#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "shaman/core/result.hpp"

namespace shaman::session {

// Undo for file changes, independent of the user's own git history.
//
// A private git directory under the data dir tracks the worktree. track()
// records the current state as a tree id; restore() puts tracked files back.
// Your repository, index and branches are never touched.
class Snapshot {
 public:
  Snapshot(std::filesystem::path worktree, std::filesystem::path git_dir);
  std::optional<std::string> track();
  Result<void> restore(const std::string& tree);
  std::string diff(const std::string& tree);  // changes since `tree`

 private:
  Result<std::string> git(const std::string& args);
  std::filesystem::path worktree_, git_dir_;
};

}  // namespace shaman::session
