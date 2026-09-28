#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"
#include "shaman/core/result.hpp"

namespace shaman {

namespace fs = std::filesystem;

struct McpServerConfig {
  std::string type = "local";                     // local (stdio) | remote (streamable HTTP)
  std::vector<std::string> command;               // local
  std::map<std::string, std::string> environment;  // local
  std::string url;                                // remote
  std::map<std::string, std::string> headers;     // remote, e.g. static Authorization
  Json oauth = true;                              // remote: false disables; object may set clientId/scope
  bool enabled = true;
  int timeout_ms = 60'000;
};

// Effective configuration after merging every layer, lowest priority first:
//   1. built-in defaults
//   2. $XDG_CONFIG_HOME/shaman/shaman.json(c)
//   3. shaman.json(c) or .shaman/shaman.json(c), walking up from cwd to the project root
//   4. $SHAMAN_CONFIG (a file path)
//   5. $SHAMAN_CONFIG_CONTENT (inline JSON)
// Objects merge key by key; later layers win. String values may use
// {env:NAME} and {file:path} substitutions.
struct Config {
  std::string model;        // "provider/model"; empty picks the best free model
  std::string default_agent = "build";
  bool free_fallback = true;  // on rate limits, hop to the next free model
  bool snapshot = true;       // git-backed undo of file changes
  Json permission = Json::object();
  Json agent = Json::object();
  Json provider = Json::object();  // reserved for key-based providers
  std::vector<std::string> instructions;
  std::map<std::string, McpServerConfig> mcp;

  Json raw = Json::object();         // merged document, for `shaman debug config`
  std::vector<fs::path> sources;     // files that contributed, in order

  static Result<Config> load(const fs::path& cwd, const fs::path& project_root);
  static Result<Config> from_json(const Json& merged);
};

// JSON with // and /* */ comments and trailing commas.
Result<Json> parse_jsonc(std::string_view text);

// Expand {env:NAME} and {file:path} (relative to `base`) inside every string.
void substitute(Json& value, const fs::path& base);

}  // namespace shaman
