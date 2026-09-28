#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "shaman/core/result.hpp"

namespace shaman::process {

// A pipe or process: a file descriptor / pid on POSIX, a HANDLE on Windows.
#ifdef _WIN32
using native_handle = std::intptr_t;
#else
using native_handle = int;
#endif

struct Output {
  int exit_code = -1;
  std::string output;  // stdout and stderr, interleaved (only stdout with separate_stderr)
  std::string error;   // stderr, with separate_stderr
  bool timed_out = false;
  bool cancelled = false;
};

struct Options {
  std::filesystem::path cwd;
  std::chrono::milliseconds timeout{120'000};
  std::atomic<bool>* cancel = nullptr;
  size_t max_output = 1 << 20;  // keep at most this many bytes
  std::optional<std::string> input;                          // fed to stdin (default: empty stdin)
  std::vector<std::pair<std::string, std::string>> env;       // extra environment variables
  bool separate_stderr = false;                               // collect stderr into Output::error
};

// Run argv directly (no shell). The child gets its own process group so a
// timeout or cancel kills everything it spawned.
Result<Output> run(const std::vector<std::string>& argv, const Options& opts);

// Run through the user's shell (bash if present, else sh).
Result<Output> shell(const std::string& command, const Options& opts);

// Absolute path of an executable on PATH, if any.
std::optional<std::filesystem::path> which(const std::string& name);

// Running inside Termux on Android (no /tmp, no desktop; termux-api tools for notifications, clipboard, URLs).
bool termux();

// Open a URL in the user's browser ($SHAMAN_OPEN overrides the opener). Returns immediately.
void open_url(const std::string& url);

// Long-lived child with piped stdin/stdout, for line-based protocols (MCP).
class Child {
 public:
  static Result<Child> spawn(const std::vector<std::string>& argv,
                             const std::vector<std::pair<std::string, std::string>>& env = {});
  Child(Child&&) noexcept;
  Child& operator=(Child&&) noexcept;
  Child(const Child&) = delete;
  ~Child();

  Result<void> write_line(const std::string& line);
  Result<void> write(std::string_view data);
  // Returns nullopt on timeout; error on EOF.
  Result<std::optional<std::string>> read_line(std::chrono::milliseconds timeout);
  // Exactly n bytes (for Content-Length framed protocols such as LSP).
  Result<std::optional<std::string>> read_exact(size_t n, std::chrono::milliseconds timeout);
  bool alive() const { return pid_ > 0; }
  void kill();

 private:
  Child() = default;
  native_handle pid_ = -1;  // process (Windows: process handle)
  native_handle in_ = -1;   // child's stdin (we write)
  native_handle out_ = -1;  // child's stdout (we read)
  native_handle job_ = -1;  // Windows: job object owning the process tree
  std::string buf_;
};

// Long-running shell command whose output is collected in the background
// (dev servers, watchers, long builds). Poll with take_output().
class Background {
 public:
  static Result<std::shared_ptr<Background>> start(const std::string& command, const std::filesystem::path& cwd);
  ~Background();
  std::string take_output();   // output produced since the last call
  bool running();
  int exit_code() const { return exit_code_; }
  Result<void> write_input(const std::string& data);  // to the job's stdin
  void kill();
  const std::string& command() const { return command_; }

 private:
  Background() = default;
  void pump();
  native_handle pid_ = -1, fd_ = -1, in_ = -1, job_ = -1;
  int exit_code_ = -1;
  std::string command_, buf_;
  std::mutex mu_;
};

}  // namespace shaman::process
