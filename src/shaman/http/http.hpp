#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "shaman/core/result.hpp"

namespace shaman::http {

struct Request {
  std::string method = "GET";
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  long timeout_s = 600;
  std::atomic<bool>* cancel = nullptr;
};

struct Response {
  long status = 0;
  std::string body;
};

// Buffered request.
Result<Response> send(const Request& req);

// Streaming request. `on_data` receives body chunks of a 2xx response and may
// return false to abort. Non-2xx bodies are buffered into Response::body
// instead so callers can surface the server's error message.
Result<Response> stream(const Request& req, const std::function<bool(std::string_view)>& on_data);

}  // namespace shaman::http
