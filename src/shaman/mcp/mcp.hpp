#pragma once

#include <memory>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"
#include "shaman/core/process.hpp"
#include "shaman/tool/tool.hpp"

namespace shaman::mcp {

// Minimal Model Context Protocol client over stdio (JSON-RPC, one message
// per line). Each server's tools are registered as "<server>_<tool>" and go
// through the "mcp" permission.
class Client {
 public:
  static Result<std::shared_ptr<Client>> connect(const std::string& name, const McpServerConfig& cfg);
  Result<Json> call(const std::string& method, const Json& params);
  Result<std::vector<Json>> list_tools();
  const std::string& name() const { return name_; }

 private:
  Client(std::string name, process::Child child) : name_(std::move(name)), child_(std::move(child)) {}
  std::string name_;
  process::Child child_;
  int next_id_ = 1;
};

// Connect every enabled server in config and register its tools. Failures
// are logged and skipped so one broken server never blocks startup.
std::vector<std::shared_ptr<Client>> load(const Config& config, tool::Registry& tools);

}  // namespace shaman::mcp
