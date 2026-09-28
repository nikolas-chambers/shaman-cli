#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"
#include "shaman/core/process.hpp"

namespace shaman::lsp {

namespace fs = std::filesystem;

struct ServerSpec {
  std::string id;
  std::vector<std::string> command;
  std::vector<std::string> extensions;  // ".py", ".ts", ...
};

struct Diagnostic {
  int line = 0, column = 0;  // 1-based
  int severity = 1;          // 1 error, 2 warning, 3 info, 4 hint
  std::string message, source;
};

// One running language server speaking JSON-RPC over stdio.
class Client {
 public:
  static Result<std::unique_ptr<Client>> start(const ServerSpec& spec, const fs::path& root);
  ~Client();

  // Open or update a file, then wait for the server's diagnostics for it.
  std::vector<Diagnostic> diagnostics(const fs::path& file, const std::string& text, std::chrono::milliseconds wait);
  Result<Json> request(const std::string& method, const Json& params, std::chrono::milliseconds wait);
  void notify(const std::string& method, const Json& params);
  const ServerSpec& spec() const { return spec_; }

 private:
  Client(ServerSpec spec, process::Child child) : spec_(std::move(spec)), child_(std::move(child)) {}
  Result<std::optional<Json>> read_message(std::chrono::milliseconds wait);
  void handle(const Json& msg);  // server->client requests and notifications
  void send(const Json& msg);

  ServerSpec spec_;
  process::Child child_;
  int next_id_ = 1;
  std::map<std::string, int> versions_;                          // uri -> version
  std::map<std::string, std::vector<Diagnostic>> diagnostics_;   // uri -> latest
  std::map<std::string, bool> fresh_;                            // uri -> got diagnostics since last change
};

// Starts servers lazily by file extension and formats diagnostics for tools.
//
// Config:
//   "lsp": { "disabled": false, "timeout": 3000,
//            "servers": { "zls": { "command": ["zls"], "extensions": [".zig"] },
//                         "pyright": { "disabled": true } } }
class Manager {
 public:
  Manager(const Config& config, fs::path root);
  ~Manager();

  // "" when no server applies or there are no errors. Otherwise a block the
  // model can act on: "Errors in src/x.py:\n  12:5 message".
  std::string report(const fs::path& file);
  // For the lsp tool. op: diagnostics | hover | definition | references | symbols.
  Result<std::string> query(const std::string& op, const fs::path& file, int line, int column);
  Client* client_for(const fs::path& file);  // starts one if needed
  const std::vector<ServerSpec>& servers() const { return specs_; }

 private:
  fs::path root_;
  bool disabled_ = false;
  std::chrono::milliseconds timeout_{3000};
  std::vector<ServerSpec> specs_;
  std::map<std::string, std::unique_ptr<Client>> clients_;
  std::map<std::string, bool> broken_;
  std::mutex mu_;
};

std::vector<ServerSpec> builtin_servers();
std::string uri(const fs::path& p);
fs::path from_uri(const std::string& uri);

}  // namespace shaman::lsp
