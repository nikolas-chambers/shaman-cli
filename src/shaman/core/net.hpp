#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "shaman/core/result.hpp"

// Just enough HTTP/1.1 server plumbing for `shaman serve` and the OAuth
// loopback callback. One request per connection (Connection: close).
namespace shaman::net {

struct Request {
  std::string method, path, query;       // path without query string
  std::map<std::string, std::string> headers;  // lower-cased names
  std::string body;
  std::string header(const std::string& name) const;
  std::string param(const std::string& name) const;  // from the query string
};

class Connection {
 public:
  explicit Connection(int fd) : fd_(fd) {}
  ~Connection();
  Connection(const Connection&) = delete;

  Result<Request> read_request(size_t max_body = 16 * 1024 * 1024);
  bool write(std::string_view data);
  bool respond(int status, std::string_view content_type, std::string_view body,
               const std::map<std::string, std::string>& extra = {});
  bool json(int status, const std::string& body) { return respond(status, "application/json", body); }
  bool start_sse();                                  // headers for text/event-stream
  bool sse(std::string_view event, std::string_view data);

 private:
  int fd_;
};

class Listener {
 public:
  // Bind host:port (port 0 picks a free one).
  static Result<std::unique_ptr<Listener>> bind(const std::string& host, int port);
  ~Listener();
  int port() const { return port_; }
  // Blocks until a client connects or timeout_ms elapses (-1 waits forever).
  std::unique_ptr<Connection> accept(int timeout_ms = -1);

 private:
  int fd_ = -1;
  int port_ = 0;
};

std::string_view status_text(int status);

}  // namespace shaman::net
