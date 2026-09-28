#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>

#include "shaman/agent/agent.hpp"
#include "shaman/config/config.hpp"
#include "shaman/lsp/lsp.hpp"
#include "shaman/plugin/plugin.hpp"
#include "shaman/mcp/mcp.hpp"
#include "shaman/provider/registry.hpp"
#include "shaman/session/runner.hpp"
#include "shaman/session/store.hpp"
#include "shaman/tool/tool.hpp"

namespace shaman::cli {

// Everything a command needs, wired once at startup.
struct App {
  std::filesystem::path cwd, root;
  Config config;
  std::unique_ptr<auth::Store> auth;
  std::unique_ptr<provider::Registry> providers;
  std::unique_ptr<agent::Registry> agents;
  tool::Registry tools;
  std::unique_ptr<session::Store> store;
  std::vector<std::shared_ptr<mcp::Client>> mcp;
  std::unique_ptr<lsp::Manager> lsp;
  plugin::Host plugins;

  // `with_runtime` starts MCP servers and plugins (skip for inspection commands).
  static Result<std::unique_ptr<App>> create(const std::filesystem::path& cwd, bool with_runtime);
  session::Services services(permission::Asker asker, std::atomic<bool>* cancel, bool allow_all,
                             std::function<Result<std::string>(const tool::Question&)> question = nullptr);
};

}  // namespace shaman::cli
