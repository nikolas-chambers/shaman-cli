#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"

namespace shaman::hooks {

// Shell commands the user runs at points in the agent loop, configured under "hooks":
//
//   "hooks": {
//     "PreToolUse":  [{"matcher": "bash|write", "command": "./scripts/guard.sh", "timeout": 30}],
//     "PostToolUse": [{"matcher": "edit|write", "command": "npx eslint --fix \"$SHAMAN_FILE\""}],
//     "UserPromptSubmit": [{"command": "cat .shaman/context.md"}],
//     "Stop": [{"command": "make -q test || { echo 'tests fail' >&2; exit 2; }"}]
//   }
//
// Each command gets the event as JSON on stdin (and SHAMAN_* environment variables). Exit 0 lets things
// proceed (for UserPromptSubmit and SessionStart, stdout is added to the model's context); exit 2 blocks,
// and stderr says why: the tool call is refused, the prompt is rejected, or for Stop the agent keeps
// working. Any other exit code is reported and ignored. A command may also print
// {"decision": "block", "reason": "..."} on stdout instead of exiting 2.
enum class Event { pre_tool_use, post_tool_use, user_prompt_submit, stop, session_start, notification };

const char* name(Event e);

struct Outcome {
  bool block = false;
  std::string reason;               // why it blocked (stderr or JSON reason)
  std::string context;              // stdout from UserPromptSubmit / SessionStart hooks
  std::vector<std::string> errors;  // hooks that failed without blocking
};

class Hooks {
 public:
  Hooks() = default;
  Hooks(const Json& config, std::filesystem::path root);
  bool empty() const { return entries_.empty(); }
  bool has(Event e) const;

  // `subject` is what the matcher is tested against (the tool name for tool events).
  Outcome run(Event e, const std::string& session_id, const std::string& subject, Json payload) const;

 private:
  struct Entry {
    Event event;
    std::string matcher;  // regex; empty or "*" matches everything
    std::string command;
    int timeout_s = 60;
  };
  std::vector<Entry> entries_;
  std::filesystem::path root_;
};

}  // namespace shaman::hooks
