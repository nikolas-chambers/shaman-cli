#pragma once

#include <filesystem>

// Agent Client Protocol (https://agentclientprotocol.com) over stdio, so
// editors such as Zed can drive shaman as their agent: `shaman acp`.
//
// Supported: initialize, session/new, session/load, session/list,
// session/prompt (streaming session/update: message and thought chunks, tool
// calls, plan, usage), session/cancel, session/set_mode (agent switch),
// session/request_permission, and MCP servers passed by the client.
namespace shaman::acp {

struct Options {
  bool allow_all = false;
};

int serve(const std::filesystem::path& cwd, const Options& opts);

}  // namespace shaman::acp
