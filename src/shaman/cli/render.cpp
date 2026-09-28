#include "shaman/cli/render.hpp"

#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>

#include "shaman/core/strings.hpp"

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace shaman::cli {

bool stdout_is_tty() { return isatty(fileno(stdout)); }
bool stdin_is_tty() { return isatty(fileno(stdin)); }

namespace {
constexpr auto kDim = "\x1b[2m", kReset = "\x1b[0m", kCyan = "\x1b[36m", kRed = "\x1b[31m", kYellow = "\x1b[33m";

std::string summarize(const llm::ToolCallPart& call) {
  for (auto key : {"command", "filePath", "pattern", "url", "path", "description"})
    if (call.input.contains(key) && call.input[key].is_string()) return call.input[key].get<std::string>();
  return "";
}
}  // namespace

TerminalEvents::TerminalEvents(bool show_reasoning) : color_(stdout_is_tty()), show_reasoning_(show_reasoning) {}

void TerminalEvents::text(std::string_view t) {
  if (in_reasoning_) {
    std::cout << (color_ ? kReset : "") << "\n";
    in_reasoning_ = false;
  }
  std::cout << t << std::flush;
  mid_line_ = !t.empty() && t.back() != '\n';
}

void TerminalEvents::reasoning(std::string_view t) {
  if (!show_reasoning_) return;
  if (!in_reasoning_) std::cout << (color_ ? kDim : "") << "thinking: ";
  in_reasoning_ = true;
  std::cout << t << std::flush;
}

void TerminalEvents::tool_start(const llm::ToolCallPart&) {
  if (mid_line_) std::cout << "\n", mid_line_ = false;
}

void TerminalEvents::tool_end(const llm::ToolCallPart& call, const tool::Output& out) {
  auto line = out.title.empty() ? call.name + " " + summarize(call) : out.title;
  if (line.size() > 120) line = line.substr(0, 117) + "...";
  if (color_) std::cout << (out.is_error ? kRed : kCyan) << (out.is_error ? "x " : "> ") << line << kReset << "\n";
  else std::cout << (out.is_error ? "x " : "> ") << line << "\n";
  if (out.is_error) {
    auto first = str::lines(out.text);
    if (!first.empty()) std::cout << (color_ ? kDim : "") << "  " << first.front() << (color_ ? kReset : "") << "\n";
  }
}

void TerminalEvents::step_end(int, const llm::Usage&, const std::string&) {}

void TerminalEvents::notice(std::string_view n) {
  if (mid_line_) std::cout << "\n", mid_line_ = false;
  std::cout << (color_ ? kYellow : "") << n << (color_ ? kReset : "") << "\n";
}

void TerminalEvents::finish() {
  if (mid_line_ || in_reasoning_) std::cout << (color_ ? kReset : "") << "\n";
  mid_line_ = in_reasoning_ = false;
}

static void emit(Json j) { std::cout << j.dump() << "\n" << std::flush; }

void JsonEvents::text(std::string_view t) { emit({{"type", "text"}, {"text", t}}); }
void JsonEvents::tool_start(const llm::ToolCallPart& c) {
  emit({{"type", "tool_start"}, {"id", c.id}, {"name", c.name}, {"input", c.input}});
}
void JsonEvents::tool_end(const llm::ToolCallPart& c, const tool::Output& o) {
  emit({{"type", "tool_end"}, {"id", c.id}, {"title", o.title}, {"is_error", o.is_error}, {"output", o.text}});
}
void JsonEvents::step_end(int step, const llm::Usage& u, const std::string& model) {
  emit({{"type", "step"}, {"step", step}, {"model", model}, {"input_tokens", u.input}, {"output_tokens", u.output}});
}
void JsonEvents::notice(std::string_view n) { emit({{"type", "notice"}, {"text", n}}); }

permission::Reply ask_terminal(const permission::Request& req) {
#ifdef _WIN32
  std::ifstream tty("CONIN$");
#else
  std::ifstream tty("/dev/tty");
#endif
  if (!tty) return permission::Reply::reject;
  std::cerr << "\n" << kYellow << "permission" << kReset << " " << req.permission << ": " << req.title
            << "\n  [y] allow once  [a] always (this session)  [n] deny > " << std::flush;
  std::string answer;
  std::getline(tty, answer);
  answer = str::lower(str::trim(answer));
  if (answer == "a" || answer == "always") return permission::Reply::always;
  if (answer == "y" || answer == "yes") return permission::Reply::once;
  return permission::Reply::reject;
}

}  // namespace shaman::cli
