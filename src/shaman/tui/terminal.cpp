#include "shaman/tui/terminal.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace shaman::tui {

#ifndef _WIN32
static termios g_orig;
static volatile sig_atomic_t g_resized = 0;
static void on_winch(int) { g_resized = 1; }
#else
static DWORD g_in_mode = 0, g_out_mode = 0;
#endif

Terminal::Terminal() {
#ifdef _WIN32
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleMode(in, &g_in_mode) || !GetConsoleMode(out, &g_out_mode)) return;
  SetConsoleMode(in, ENABLE_VIRTUAL_TERMINAL_INPUT);
  SetConsoleMode(out, g_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  SetConsoleOutputCP(CP_UTF8);
#else
  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &g_orig) != 0) return;
  termios raw = g_orig;
  raw.c_iflag &= ~tcflag_t(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~tcflag_t(OPOST);
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~tcflag_t(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  std::signal(SIGWINCH, on_winch);
#endif
  ok_ = true;
  // Alternate screen, bracketed paste, hide cursor until we place it.
  write("\x1b[?1049h\x1b[?2004h\x1b[H\x1b[2J");
  flush();
}

Terminal::~Terminal() {
  if (!ok_) return;
  if (mouse_) write("\x1b[?1000l\x1b[?1006l");
  write("\x1b[?2004l\x1b[?25h\x1b[?1049l");
  flush();
#ifdef _WIN32
  SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), g_in_mode);
  SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_out_mode);
#else
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
#endif
}

Size Terminal::size() const {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return {info.srWindow.Bottom - info.srWindow.Top + 1, info.srWindow.Right - info.srWindow.Left + 1};
  return {};
#else
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return {ws.ws_row, ws.ws_col};
  return {};
#endif
}

void Terminal::write(const std::string& s) { out_ += s; }

void Terminal::flush() {
  fwrite(out_.data(), 1, out_.size(), stdout);
  fflush(stdout);
  out_.clear();
}

static bool read_more(std::string& buf, int timeout_ms) {
#ifdef _WIN32
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  if (WaitForSingleObject(in, timeout_ms < 0 ? INFINITE : DWORD(timeout_ms)) != WAIT_OBJECT_0) return false;
  char c[256];
  DWORD n = 0;
  if (!ReadFile(in, c, sizeof c, &n, nullptr) || n == 0) return false;
  buf.append(c, n);
  return true;
#else
  pollfd pfd{STDIN_FILENO, POLLIN, 0};
  if (poll(&pfd, 1, timeout_ms) <= 0) return false;
  char c[4096];
  ssize_t n = ::read(STDIN_FILENO, c, sizeof c);
  if (n <= 0) return false;
  buf.append(c, size_t(n));
  return true;
#endif
}

Key Terminal::read(int timeout_ms) {
#ifndef _WIN32
  if (g_resized) {
    g_resized = 0;
    return {KeyType::resize};
  }
#endif
  if (pending_.empty() && !read_more(pending_, timeout_ms)) {
#ifndef _WIN32
    if (g_resized) {
      g_resized = 0;
      return {KeyType::resize};
    }
#endif
    return {};
  }
  // Escape sequences may arrive split; give them a moment to complete.
  if (pending_[0] == '\x1b' && pending_.size() < 3) read_more(pending_, 15);
  return parse();
}

void Terminal::enable_mouse(bool on) {
  mouse_ = on;
  write(on ? "\x1b[?1000h\x1b[?1006h" : "\x1b[?1000l\x1b[?1006l");
  flush();
}

Key Terminal::parse() {
  auto take = [&](size_t n) { pending_.erase(0, n); };
  unsigned char c = pending_[0];
  if (pending_.starts_with("\x1b[200~")) {  // bracketed paste
    auto end = pending_.find("\x1b[201~");
    while (end == std::string::npos && read_more(pending_, 100)) end = pending_.find("\x1b[201~");
    std::string text = pending_.substr(6, end == std::string::npos ? std::string::npos : end - 6);
    take(end == std::string::npos ? pending_.size() : end + 6);
    std::erase(text, '\r');
    return {KeyType::paste, text};
  }
  if (c == '\x1b') {
    struct Seq { const char* s; KeyType t; };
    static const Seq seqs[] = {{"\x1b[A", KeyType::up},    {"\x1b[B", KeyType::down},  {"\x1b[C", KeyType::right},
                               {"\x1b[D", KeyType::left},  {"\x1b[H", KeyType::home},  {"\x1b[F", KeyType::end},
                               {"\x1bOA", KeyType::up},    {"\x1bOB", KeyType::down},  {"\x1bOC", KeyType::right},
                               {"\x1bOD", KeyType::left},  {"\x1bOH", KeyType::home},  {"\x1bOF", KeyType::end},
                               {"\x1b[1~", KeyType::home}, {"\x1b[4~", KeyType::end},  {"\x1b[3~", KeyType::del},
                               {"\x1b[5~", KeyType::pgup}, {"\x1b[6~", KeyType::pgdn}, {"\x1b[Z", KeyType::shift_tab}};
    for (auto& s : seqs)
      if (pending_.starts_with(s.s)) {
        take(std::strlen(s.s));
        return {s.t};
      }
    if (pending_.starts_with("\x1b[<")) {  // SGR mouse: ESC [ < button ; x ; y (M|m)
      size_t end = 3;
      while (end < pending_.size() && pending_[end] != 'M' && pending_[end] != 'm') ++end;
      if (end == pending_.size() && read_more(pending_, 15))
        while (end < pending_.size() && pending_[end] != 'M' && pending_[end] != 'm') ++end;
      int button = std::atoi(pending_.c_str() + 3);
      take(std::min(end + 1, pending_.size()));
      if (button == 64) return {KeyType::wheel_up};
      if (button == 65) return {KeyType::wheel_down};
      return {};
    }
    if (pending_.size() >= 2 && pending_[1] == '\r') {  // alt+enter: newline
      take(2);
      return {KeyType::enter, "", 0, true};
    }
    if (pending_.size() >= 2 && pending_[1] == '[') {  // unknown CSI: swallow
      size_t i = 2;
      while (i < pending_.size() && !(pending_[i] >= 0x40 && pending_[i] <= 0x7e)) ++i;
      take(std::min(i + 1, pending_.size()));
      return {};
    }
    if (pending_.size() >= 2) {
      Key k{KeyType::ch, std::string(1, pending_[1]), 0, true};
      take(2);
      return k;
    }
    take(1);
    return {KeyType::esc};
  }
  if (c == '\r') return take(1), Key{KeyType::enter};
  if (c == '\n') return take(1), Key{KeyType::enter, "", 0, true};  // ctrl+j: newline
  if (c == '\t') return take(1), Key{KeyType::tab};
  if (c == 127 || c == 8) return take(1), Key{KeyType::backspace};
  if (c == 4 && pending_.size() == 1) return take(1), Key{KeyType::eof};
  if (c < 32) return take(1), Key{KeyType::ctrl, "", char('a' + c - 1)};
  size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : 4;
  if (pending_.size() < len) read_more(pending_, 15);
  Key k{KeyType::ch, pending_.substr(0, len)};
  take(std::min(len, pending_.size()));
  return k;
}

}  // namespace shaman::tui
