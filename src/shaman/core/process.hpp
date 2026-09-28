#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "shaman/core/result.hpp"

namespace shaman::process {

struct Output {
  int exit_code = -1;
  std::string output;  // stdout and stderr, interleaved
  bool timed_out = false;
  bool cancelled = false;
};

struct Options {
  std::filesystem::path cwd;
  std::chrono::milliseconds timeout{120'000};
  std::atomic<bool>* cancel = nullptr;
  size_t max_output = 1 << 20;  // keep at most this many bytes
};

// Run argv directly (no shell). The child gets its own process group so a
// timeout or cancel kills everything it spawned.
Result<Output> run(const std::vector<std::string>& argv, const Options& opts);

// Run through the user's shell (bash if present, else sh).
Result<Output> shell(const std::string& command, const Options& opts);

// Absolute path of an executable on PATH, if any.
std::optional<std::filesystem::path> which(const std::string& name);

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
  // Returns nullopt on timeout; error on EOF.
  Result<std::optional<std::string>> read_line(std::chrono::milliseconds timeout);
  void kill();

 private:
  Child() = default;
  int pid_ = -1;
  int in_ = -1;   // child's stdin (we write)
  int out_ = -1;  // child's stdout (we read)
  std::string buf_;
};

}  // namespace shaman::process
