#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"
#include "shaman/core/result.hpp"
#include "shaman/llm/message.hpp"
#include "shaman/permission/permission.hpp"

namespace shaman::tool {

namespace fs = std::filesystem;

struct Todo {
  std::string id, content, status, priority;  // status: pending|in_progress|completed|cancelled
};

// Everything a tool may touch during one call. Owned by the session runner.
struct Context {
  fs::path root;                 // project root; paths resolve against it
  std::string session_id;
  permission::Gate* gate = nullptr;
  std::atomic<bool>* cancel = nullptr;
  std::set<fs::path>* read_files = nullptr;  // edit/write require a prior read
  std::vector<Todo>* todos = nullptr;
  // Runs a subagent to completion and returns its final answer.
  std::function<Result<std::string>(const std::string& agent, const std::string& prompt)> subagent;

  bool permit(std::string permission, std::string subject, std::string title) const;
  // Resolve a user/model supplied path; asks before leaving the project.
  Result<fs::path> path(const std::string& p) const;
};

struct Output {
  std::string text;
  bool is_error = false;
  std::string title;  // one-line summary for the UI ("Read src/main.cpp")
};

inline Output error(std::string msg) { return {std::move(msg), true, ""}; }

class Tool {
 public:
  virtual ~Tool() = default;
  virtual std::string name() const = 0;
  virtual std::string description() const = 0;
  virtual Json schema() const = 0;  // JSON Schema for the input object
  virtual Output run(const Json& input, Context& ctx) = 0;

  llm::ToolSpec spec() const { return {name(), description(), schema()}; }
};

class Registry {
 public:
  void add(std::unique_ptr<Tool> tool);
  Tool* find(const std::string& name) const;
  std::vector<Tool*> all() const;

 private:
  std::map<std::string, std::unique_ptr<Tool>> tools_;
};

// read, write, edit, list, glob, grep, bash, webfetch, todowrite, todoread, task
void register_builtins(Registry& registry);

// Cap tool output so one call can't blow the context window.
std::string truncate(std::string text, size_t max_lines = 2000, size_t max_bytes = 50 * 1024);

}  // namespace shaman::tool
