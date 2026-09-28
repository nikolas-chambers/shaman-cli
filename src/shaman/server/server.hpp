#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "shaman/cli/app.hpp"

// Headless HTTP API (`shaman serve`). JSON over HTTP, streaming via SSE.
//
//   GET    /health                          {"version": ...}
//   GET    /models | /agents | /commands | /config
//   GET    /session                         list sessions
//   POST   /session                         {"agent"?, "model"?} -> session info
//   GET    /session/:id                     session info
//   DELETE /session/:id
//   GET    /session/:id/message             full message log
//   POST   /session/:id/message             {"text", "model"?, "agent"?, "files"?} -> SSE stream
//   POST   /session/:id/abort               cancel the running turn
//   POST   /session/:id/undo | /compact
//   POST   /permission/:id                  {"reply": "once"|"always"|"reject"}
//   GET    /event                           SSE stream of every session's events
//
// Stream events: text, reasoning, tool_start, tool_end, step, notice,
// permission, done, error. See docs/SERVER.md and sdk/.
namespace shaman::server {

struct Options {
  std::string host = "127.0.0.1";
  int port = 4096;
  std::string token;  // require "Authorization: Bearer <token>" when set
  bool allow_all = false;
  std::function<void(const std::string& url)> on_listen;  // e.g. open a browser
};

int serve(cli::App& app, const Options& opts);

}  // namespace shaman::server
