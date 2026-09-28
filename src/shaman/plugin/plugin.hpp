#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"
#include "shaman/core/process.hpp"
#include "shaman/permission/permission.hpp"
#include "shaman/tool/tool.hpp"

// Plugins: extend shaman without recompiling it.
//
// A plugin is any executable that speaks newline-delimited JSON-RPC 2.0 on
// stdio (see docs/PLUGINS.md and the SDKs in sdk/). It can
//   - rewrite or block tool calls        (tool.before)
//   - rewrite tool output                (tool.after)
//   - decide permissions                 (permission.ask)
//   - rewrite the system prompt          (chat.system)
//   - rewrite user prompts               (chat.message)
//   - observe everything                 (event notifications)
//   - add tools                          (tool.call)
//
// C++ embedders can implement Hooks directly and add it to the Host.
namespace shaman::plugin {

namespace fs = std::filesystem;

struct ToolBefore {
  std::string session, tool;
  Json input;
  std::optional<std::string> block;  // set to refuse the call with this reason
};

struct ToolAfter {
  std::string session, tool;
  Json input;
  std::string output;
  bool is_error = false;
};

class Hooks {
 public:
  virtual ~Hooks() = default;
  virtual std::string name() const = 0;
  virtual void tool_before(ToolBefore&) {}
  virtual void tool_after(ToolAfter&) {}
  virtual std::optional<permission::Action> permission_ask(const permission::Request&) { return std::nullopt; }
  virtual void chat_system(const std::string& /*agent*/, std::string& /*system*/) {}
  virtual void chat_message(const std::string& /*session*/, std::string& /*text*/) {}
  virtual void event(const std::string& /*type*/, const Json& /*data*/) {}
};

// Out-of-process plugin.
class ProcessPlugin final : public Hooks, public std::enable_shared_from_this<ProcessPlugin> {
 public:
  static Result<std::shared_ptr<ProcessPlugin>> start(const std::vector<std::string>& command, const fs::path& root);

  std::string name() const override { return name_; }
  void tool_before(ToolBefore& ev) override;
  void tool_after(ToolAfter& ev) override;
  std::optional<permission::Action> permission_ask(const permission::Request& req) override;
  void chat_system(const std::string& agent, std::string& system) override;
  void chat_message(const std::string& session, std::string& text) override;
  void event(const std::string& type, const Json& data) override;

  const std::vector<Json>& tools() const { return tools_; }
  Result<Json> call(const std::string& method, const Json& params, std::chrono::milliseconds timeout);

 private:
  ProcessPlugin(process::Child child) : child_(std::move(child)) {}
  bool wants(const std::string& hook) const;
  std::string name_;
  std::vector<std::string> hooks_;
  std::vector<Json> tools_;
  process::Child child_;
  int next_id_ = 1;
  std::mutex mu_;
};

// Runs every hook in registration order. Plugin failures are logged, never fatal.
class Host {
 public:
  void add(std::shared_ptr<Hooks> hooks);
  // Start plugins from config `plugin` and .shaman/plugins/, register their tools.
  void load(const Config& config, const fs::path& root, tool::Registry& tools);

  void tool_before(ToolBefore& ev);
  void tool_after(ToolAfter& ev);
  std::optional<permission::Action> permission_ask(const permission::Request& req);
  void chat_system(const std::string& agent, std::string& system);
  void chat_message(const std::string& session, std::string& text);
  void event(const std::string& type, const Json& data);

  const std::vector<std::shared_ptr<Hooks>>& plugins() const { return hooks_; }
  bool empty() const { return hooks_.empty(); }

 private:
  std::vector<std::shared_ptr<Hooks>> hooks_;
};

}  // namespace shaman::plugin
