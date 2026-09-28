#pragma once

#include <filesystem>
#include <string>

#include "shaman/core/result.hpp"

// Isolated git worktrees for parallel work (like opencode-worktree):
//   shaman worktree <name>        create ../<repo>-<name> on branch shaman/<name> and open a session there
//   shaman worktree list | remove <name>
namespace shaman::extras {

Result<std::filesystem::path> worktree_create(const std::filesystem::path& root, const std::string& name);
Result<std::string> worktree_list(const std::filesystem::path& root);
Result<void> worktree_remove(const std::filesystem::path& root, const std::string& name);

}  // namespace shaman::extras
