#pragma once

#include <string>

#include "shaman/permission/permission.hpp"
#include "shaman/session/runner.hpp"

namespace shaman::cli {

// Plain streaming terminal output. Colours only when stdout is a TTY.
class TerminalEvents final : public session::Events {
 public:
  explicit TerminalEvents(bool show_reasoning = false);
  void text(std::string_view t) override;
  void reasoning(std::string_view t) override;
  void tool_start(const llm::ToolCallPart& call) override;
  void tool_end(const llm::ToolCallPart& call, const tool::Output& out) override;
  void step_end(int step, const llm::Usage& usage, const std::string& model) override;
  void notice(std::string_view n) override;
  void finish();  // newline after streamed text

 private:
  bool color_, show_reasoning_, mid_line_ = false, in_reasoning_ = false;
};

// Emits one JSON object per event on stdout (`run --format json`).
class JsonEvents final : public session::Events {
 public:
  void text(std::string_view t) override;
  void tool_start(const llm::ToolCallPart& call) override;
  void tool_end(const llm::ToolCallPart& call, const tool::Output& out) override;
  void step_end(int step, const llm::Usage& usage, const std::string& model) override;
  void notice(std::string_view n) override;
};

// Ask on the controlling terminal; with no terminal, reject.
permission::Reply ask_terminal(const permission::Request& req);
// Question tool on the controlling terminal; error when there is none.
Result<std::string> question_terminal(const tool::Question& q);

bool stdout_is_tty();
bool stdin_is_tty();

}  // namespace shaman::cli
