#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <optional>
#include <string>

#include "shaman/agent/agent.hpp"
#include "shaman/config/config.hpp"
#include "shaman/lsp/lsp.hpp"
#include "shaman/plugin/plugin.hpp"
#include "shaman/permission/permission.hpp"
#include "shaman/provider/registry.hpp"
#include "shaman/session/store.hpp"
#include "shaman/tool/tool.hpp"

namespace shaman::hooks { class Hooks; }

namespace shaman::session {

/// A short session title from the first message: first line, at most `max` bytes, cut at a word boundary.
std::string make_title(std::string_view text, size_t max = 60);


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
  bool allow_all = false;          // --yolo
  lsp::Manager* lsp = nullptr;     // optional: diagnostics after edits, lsp tool
  plugin::Host* plugins = nullptr; // optional: plugin hooks
  std::function<Result<std::string>(const tool::Question&)> question;  // optional: the question tool
  const hooks::Hooks* hooks = nullptr;  // optional: user shell hooks (config "hooks")
};

// How much of the context window to use, from config "context" and the model's own "context" options:
//   { "limit": 32000, "pruneAt": 0.6, "compactAt": 0.85, "keepToolOutputs": 6, "maxToolOutput": 20000 }
struct ContextSettings {
  int64_t limit = 0;             // treat the window as this many tokens (0 = the model's full window)
  double prune_at = 0.6;         // above this share, old tool outputs are replaced by a stub in requests
  double compact_at = 0.85;      // above this share, the conversation is summarised
  int keep_tool_outputs = 6;     // most recent tool outputs always kept whole
  size_t max_tool_output = 0;    // cap each tool output at this many characters (0 = the tool's own limit)
  static ContextSettings resolve(const Json& config_context, const Json& model_context);
};

struct PromptOptions {
  std::optional<std::string> model;     // provider/model override for this turn
  std::vector<std::string> attachments; // extra files/images (--file)
};

// The agent loop: send the conversation, stream the reply, run requested
// tools, feed results back, repeat until the model stops calling tools.
class Runner {
 public:
  explicit Runner(Services services);

  // Run one user turn. Returns the final assistant text.
  Result<std::string> prompt(Info& session, const std::string& text, Events& events, const PromptOptions& opts = {});

  // Replace history with a summary to free context.
  Result<void> compact(Info& session, Events& events);

  // Restore files to the snapshot taken before the last user turn.
  Result<std::string> undo(Info& session);

  struct UserTurn {
    size_t index;       // 0-based turn number
    std::string text;   // what the user typed
  };
  std::vector<UserTurn> turns(const Info& session) const;
  // Go back to before turn `n`: files restored to that point (when a snapshot
  // exists) and the conversation truncated. Returns the text of that turn.
  Result<std::string> revert(Info& session, size_t n);
  // New session with the conversation up to (not including) turn `n`; all of it when n is past the end.
  Result<Info> fork(const Info& session, size_t n);

  std::vector<tool::Todo>& todos() { return todos_; }

  // What the next request would spend its context on (estimated tokens), for /context.
  struct ContextReport {
    std::string model;
    int64_t window = 0, system = 0, tools = 0, messages = 0, tool_outputs = 0;
    size_t tool_count = 0, message_count = 0;
  };
  Result<ContextReport> context_report(const Info& session, const std::optional<std::string>& model = std::nullopt);

  // Permission mode for every agent's gate; safe to change while a turn runs.
  void set_mode(permission::Mode m) { mode_ = m; }
  permission::Mode mode() const { return mode_; }

  // Reasoning effort for every request from now on: low | medium | high | off; "" = model/agent default.
  void set_effort(std::string e) { std::lock_guard l(effort_mu_); effort_ = std::move(e); }
  std::string effort() const { std::lock_guard l(effort_mu_); return effort_; }

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
  std::atomic<permission::Mode> mode_{permission::Mode::normal};
  std::optional<std::string> pending_agent_;
  mutable std::mutex effort_mu_;
  std::string effort_;  // set by plan_exit, applied after the tool results
  std::map<std::string, std::unique_ptr<permission::Gate>> gates_;
  std::set<std::filesystem::path> read_files_;
  std::vector<tool::Todo> todos_;
  std::vector<std::string> recent_calls_;  // doom-loop detection
  std::map<std::string, std::shared_ptr<process::Background>> jobs_;  // bash background=true

  struct BackgroundTask {
    std::thread thread;
    std::mutex mu;
    bool done = false;
    std::string result;
  };
  std::map<std::string, std::shared_ptr<BackgroundTask>> tasks_;  // task background=true

 public:
  ~Runner();
};

}  // namespace shaman::session
