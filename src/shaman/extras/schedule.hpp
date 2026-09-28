#pragma once

#include <filesystem>
#include <string>

#include "shaman/core/result.hpp"

// Recurring runs via the user's crontab (Linux, macOS), like opencode-scheduler:
//   shaman schedule add "0 9 * * 1-5" "summarise yesterday's commits into NOTES.md" [--model m] [--yolo]
//   shaman schedule list | remove <id>
// Output of each run is appended to $XDG_DATA_HOME/shaman/schedule/<id>.log.
namespace shaman::extras {

Result<std::string> schedule_add(const std::filesystem::path& root, const std::string& cron, const std::string& prompt,
                                 const std::string& extra_flags);
Result<std::string> schedule_list();
Result<void> schedule_remove(const std::string& id);

// Pure helper, unit tested: validates a 5-field cron expression.
bool valid_cron(const std::string& expr);

}  // namespace shaman::extras
