#include "shaman/server/pair.hpp"

#include "qrcodegen.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace shaman::server {

std::string lan_ip() {
  // A UDP "connect" sends nothing; it only makes the OS pick the outgoing interface.
#ifdef _WIN32
  WSADATA d;
  WSAStartup(MAKEWORD(2, 2), &d);
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET) return "127.0.0.1";
#else
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return "127.0.0.1";
#endif
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(53);
  inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);  // TEST-NET-1: never actually contacted
  std::string ip = "127.0.0.1";
  if (connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof remote) == 0) {
    sockaddr_in local{};
    socklen_t len = sizeof local;
    char buf[INET_ADDRSTRLEN] = {};
    if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0 && inet_ntop(AF_INET, &local.sin_addr, buf, sizeof buf))
      ip = buf;
  }
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
  return ip == "0.0.0.0" ? "127.0.0.1" : ip;
}

std::string qr_terminal(const std::string& text, bool unicode) {
  using qrcodegen::QrCode;
  auto qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::LOW);
  const int n = qr.getSize(), q = 2;  // quiet zone
  auto dark = [&](int x, int y) { return x >= 0 && y >= 0 && x < n && y < n && qr.getModule(x, y); };
  std::string out;
  if (!unicode) {
    for (int y = -q; y < n + q; ++y) {
      for (int x = -q; x < n + q; ++x) out += dark(x, y) ? "##" : "  ";
      out += "\n";
    }
    return out;
  }
  // Explicit black on white, whatever the terminal theme, so every camera reads it.
  for (int y = -q; y < n + q; y += 2) {
    out += "\x1b[38;5;16;48;5;231m";
    for (int x = -q; x < n + q; ++x) {
      bool top = dark(x, y), bottom = dark(x, y + 1);
      out += top && bottom ? "\u2588" : top ? "\u2580" : bottom ? "\u2584" : " ";
    }
    out += "\x1b[0m\n";
  }
  return out;
}

}  // namespace shaman::server
