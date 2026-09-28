#include "shaman/core/net.hpp"

#include <cstring>
#include <format>

#include "shaman/core/strings.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#define CLOSESOCK closesocket
static void net_init() {
  static bool done = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
  (void)done;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK ::close
static void net_init() {}
#endif

namespace shaman::net {

std::string Request::header(const std::string& name) const {
  auto it = headers.find(str::lower(name));
  return it == headers.end() ? "" : it->second;
}

std::string Request::param(const std::string& name) const {
  for (auto& kv : str::split(query, '&')) {
    auto eq = kv.find('=');
    if (str::url_decode(kv.substr(0, eq)) == name) return eq == std::string::npos ? "" : str::url_decode(kv.substr(eq + 1));
  }
  return "";
}

Connection::~Connection() {
  if (fd_ >= 0) CLOSESOCK(fd_);
}

Result<Request> Connection::read_request(size_t max_body) {
  std::string buf;
  char chunk[8192];
  size_t header_end;
  while ((header_end = buf.find("\r\n\r\n")) == std::string::npos) {
    auto n = recv(fd_, chunk, sizeof chunk, 0);
    if (n <= 0) return fail("connection closed");
    buf.append(chunk, size_t(n));
    if (buf.size() > 64 * 1024) return fail("headers too large");
  }
  Request req;
  auto ls = str::split(buf.substr(0, header_end), '\n');
  auto first = str::split(str::trim(ls[0]), ' ');
  if (first.size() < 2) return fail("bad request line");
  req.method = first[0];
  auto target = first[1];
  auto q = target.find('?');
  req.path = str::url_decode(target.substr(0, q));
  if (q != std::string::npos) req.query = target.substr(q + 1);
  for (size_t i = 1; i < ls.size(); ++i) {
    auto colon = ls[i].find(':');
    if (colon == std::string::npos) continue;
    req.headers[str::lower(str::trim(ls[i].substr(0, colon)))] = str::trim(ls[i].substr(colon + 1));
  }
  size_t length = 0;
  if (auto cl = req.header("content-length"); !cl.empty()) length = std::stoul(cl);
  if (length > max_body) return fail("body too large");
  req.body = buf.substr(header_end + 4);
  while (req.body.size() < length) {
    auto n = recv(fd_, chunk, sizeof chunk, 0);
    if (n <= 0) return fail("connection closed");
    req.body.append(chunk, size_t(n));
  }
  req.body.resize(length);
  return req;
}

bool Connection::write(std::string_view data) {
  size_t off = 0;
  while (off < data.size()) {
#ifdef MSG_NOSIGNAL
    auto n = send(fd_, data.data() + off, data.size() - off, MSG_NOSIGNAL);
#else
    auto n = send(fd_, data.data() + off, int(data.size() - off), 0);
#endif
    if (n <= 0) return false;
    off += size_t(n);
  }
  return true;
}

std::string_view status_text(int s) {
  switch (s) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    default: return s >= 500 ? "Internal Server Error" : "Unknown";
  }
}

bool Connection::respond(int status, std::string_view type, std::string_view body,
                         const std::map<std::string, std::string>& extra) {
  std::string head = std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n"
                                 "Access-Control-Allow-Origin: *\r\n",
                                 status, status_text(status), type, body.size());
  for (auto& [k, v] : extra) head += k + ": " + v + "\r\n";
  return write(head + "\r\n") && write(body);
}

bool Connection::start_sse() {
  return write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
               "Connection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
}

bool Connection::sse(std::string_view event, std::string_view data) {
  std::string out = event.empty() ? "" : std::format("event: {}\n", event);
  for (auto& line : str::split(data, '\n')) out += "data: " + line + "\n";
  return write(out + "\n");
}

Result<std::unique_ptr<Listener>> Listener::bind(const std::string& host, int port) {
  net_init();
  int fd = int(socket(AF_INET, SOCK_STREAM, 0));
  if (fd < 0) return fail("socket failed");
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  if (inet_pton(AF_INET, host == "localhost" ? "127.0.0.1" : host.c_str(), &addr.sin_addr) != 1) {
    CLOSESOCK(fd);
    return fail("bad listen address: " + host);
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 64) != 0) {
    CLOSESOCK(fd);
    return fail(std::format("cannot listen on {}:{}", host, port));
  }
  socklen_t len = sizeof addr;
  getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
  auto l = std::unique_ptr<Listener>(new Listener());
  l->fd_ = fd;
  l->port_ = ntohs(addr.sin_port);
  return l;
}

Listener::~Listener() {
  if (fd_ >= 0) CLOSESOCK(fd_);
}

std::unique_ptr<Connection> Listener::accept(int timeout_ms) {
#ifndef _WIN32
  if (timeout_ms >= 0) {
    pollfd pfd{fd_, POLLIN, 0};
    if (poll(&pfd, 1, timeout_ms) <= 0) return nullptr;
  }
#endif
  int c = int(::accept(fd_, nullptr, nullptr));
  if (c < 0) return nullptr;
  return std::make_unique<Connection>(c);
}

}  // namespace shaman::net
