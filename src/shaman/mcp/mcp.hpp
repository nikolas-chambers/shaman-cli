#pragma once

#include <memory>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"
#include "shaman/core/process.hpp"
#include "shaman/tool/tool.hpp"

namespace shaman::mcp {

// How JSON-RPC messages reach a server.
class Transport {
 public:
  virtual ~Transport() = default;
  virtual Result<Json> request(const Json& msg, std::chrono::milliseconds timeout) = 0;  // returns the response
  virtual void notify(const Json& msg) = 0;
};

// Model Context Protocol client. Local servers run as child processes (stdio,
// one JSON message per line); remote servers use Streamable HTTP, with OAuth
// when the server asks for it (`shaman mcp auth <name>`). Each server's tools
// are registered as "<server>_<tool>" behind the "mcp" permission.
class Client {
 public:
  static Result<std::shared_ptr<Client>> connect(const std::string& name, const McpServerConfig& cfg);
  Result<Json> call(const std::string& method, const Json& params);
  Result<std::vector<Json>> list_tools();
  const std::string& name() const { return name_; }

 private:
  Client(std::string name, std::unique_ptr<Transport> t, std::chrono::milliseconds timeout)
      : name_(std::move(name)), transport_(std::move(t)), timeout_(timeout) {}
  std::string name_;
  std::unique_ptr<Transport> transport_;
  std::chrono::milliseconds timeout_;
  int next_id_ = 1;
  std::mutex mu_;
};

// Connect every enabled server in config and register its tools. Failures
// are logged and skipped so one broken server never blocks startup.
std::vector<std::shared_ptr<Client>> load(const Config& config, tool::Registry& tools);

// Serve shaman's own tools over MCP stdio (`shaman mcp serve`).
int serve(const Config& config, const std::filesystem::path& root, tool::Registry& tools, bool allow_all);

}  // namespace shaman::mcp
