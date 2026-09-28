#pragma once

#include <filesystem>
#include <string>

#include "shaman/cli/app.hpp"

// GitHub integration.
//
//   shaman github install    add .github/workflows/shaman.yml to this repo
//   shaman github run        (inside GitHub Actions) answer a "/shaman ..." comment
//                            on an issue or PR: run the agent in the checkout,
//                            push a branch + open a PR (issues) or push to the
//                            PR branch, and reply with a summary
//   shaman pr <number>       check out a PR with `gh` and start a session on it
//
// `run` reads GITHUB_EVENT_PATH, GITHUB_REPOSITORY and GITHUB_TOKEN; the
// trigger phrase is "/shaman" (or $SHAMAN_TRIGGER).
namespace shaman::github {

int install(const std::filesystem::path& root);
int run(cli::App& app, bool dry_run);

// Pure helpers, unit tested.
std::string extract_prompt(const std::string& comment, const std::string& trigger);  // "" if not triggered
std::string branch_name(const std::string& kind, int number, const std::string& session_id);

}  // namespace shaman::github
