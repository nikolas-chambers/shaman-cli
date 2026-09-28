#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "shaman/core/json.hpp"

// Debug logging and tracing.
//
// Logging is split into categories so you can watch one subsystem at a time:
//
//   SHAMAN_DEBUG=provider,tool shaman run "..."
//   shaman --debug=http,sse run "..."
//   shaman --debug run "..."            (all categories)
//
// Tracing writes one JSON object per line for every request, stream event,
// tool call and permission decision, so a whole run can be replayed or diffed:
//
//   shaman --trace run.jsonl run "..."
namespace shaman::log {

enum class Cat : unsigned {
  config = 1u << 0,      // config layers, merges, variable substitution
  provider = 1u << 1,    // model resolution, retries, fallbacks
  http = 1u << 2,        // requests, statuses, timings
  sse = 1u << 3,         // raw server-sent events
  tool = 1u << 4,        // tool inputs, outputs, durations
  permission = 1u << 5,  // rule evaluation and replies
  session = 1u << 6,     // loop steps, compaction, persistence
  agent = 1u << 7,       // agent loading, prompt assembly
  mcp = 1u << 8,         // MCP JSON-RPC traffic
  all = 0xffffffffu,
};

// Parse "http,tool" / "all" / "1" into a mask. Unknown names are ignored.
unsigned parse_categories(std::string_view spec);
std::string_view name(Cat c);

void enable(unsigned mask);
bool enabled(Cat c);
void set_file(const std::filesystem::path& path);  // default: stderr

void write(Cat c, std::string_view message);

template <class... Args>
void debug(Cat c, std::format_string<Args...> fmt, Args&&... args) {
  if (enabled(c)) write(c, std::format(fmt, std::forward<Args>(args)...));
}

void warn(std::string_view message);  // always shown

// Structured trace sink (JSONL). No-op until open() is called.
void trace_open(const std::filesystem::path& path);
bool tracing();
void trace(std::string_view kind, Json data);

}  // namespace shaman::log
