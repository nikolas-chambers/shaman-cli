#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <optional>
#include <string>

#include "shaman/agent/agent.hpp"
#include "shaman/config/config.hpp"
#include "shaman/permission/permission.hpp"
#include "shaman/provider/registry.hpp"
#include "shaman/session/store.hpp"
#include "shaman/tool/tool.hpp"

namespace shaman::session {

// What the runner reports while it works. The CLI renders these; tests and
// other front ends (a future TUI or server) implement their own.
class Events {
 public:
  virtual ~Events() = default;
  virtual void text(std::string_view) {}
  virtual void reasoning(std::string_view) {}
  virtual void tool_start(const llm::ToolCallPart&) {}
  virtual void tool_end(const llm::ToolCallPart&, const tool::Output&) {}
  virtual void step_end(int /*step*/, const llm::Usage&, const std::string& /*model*/) {}
  virtual void notice(std::string_view) {}  // retries, model fallbacks, compaction
};

struct Services {
  std::filesystem::path root;
  const Config* config;
  const provider::Registry* providers;
  const agent::Registry* agents;
  tool::Registry* tools;
  Store* store;
  permission::Asker asker;
  std::atomic<bool>* cancel = nullptr;
  bool allow_all = false;  // --yolo
};

// The agent loop: send the conversation, stream the reply, run requested
// tools, feed results back, repeat until the model stops calling tools.
class Runner {
 public:
  explicit Runner(Services services);

  // Run one user turn. Returns the final assistant text.
  Result<std::string> prompt(Info& session, const std::string& text, Events& events,
                             const std::optional<std::string>& model = std::nullopt);

  // Replace history with a summary to free context.
  Result<void> compact(Info& session, Events& events);

  // Restore files to the snapshot taken before the last user turn.
  Result<std::string> undo(Info& session);

  std::vector<tool::Todo>& todos() { return todos_; }

 private:
  struct Turn {
    llm::Message message{llm::Role::assistant, {}};
    llm::Usage usage;
    llm::Finish finish = llm::Finish::unknown;
  };

  Result<provider::Resolved> pick_model(const Info& session, const agent::Agent& agent,
                                        const std::optional<std::string>& override_ref) const;
  Result<Turn> complete(provider::Resolved& model, const llm::ChatRequest& base, Events& events);
  permission::Gate& gate_for(const agent::Agent& agent);
  Result<std::string> prompt_as_subagent(Info& child, const std::string& text, Events& events);
  tool::Output run_tool(const llm::ToolCallPart& call, const agent::Agent& agent, const Info& session, Events& events);

  Services s_;
  std::map<std::string, std::unique_ptr<permission::Gate>> gates_;
  std::set<std::filesystem::path> read_files_;
  std::vector<tool::Todo> todos_;
  std::vector<std::string> recent_calls_;  // doom-loop detection
};

}  // namespace shaman::session
