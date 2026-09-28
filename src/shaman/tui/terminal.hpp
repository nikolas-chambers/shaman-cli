#pragma once

#include <string>

// Raw terminal access for the full-screen UI. POSIX uses termios; Windows
// uses the console API with virtual-terminal sequences.
namespace shaman::tui {

struct Size {
  int rows = 24, cols = 80;
};

enum class KeyType { none, ch, enter, backspace, del, tab, shift_tab, esc, up, down, left, right, home, end, pgup, pgdn,
                     ctrl, paste, resize, eof, wheel_up, wheel_down };

struct Key {
  KeyType type = KeyType::none;
  std::string text;  // UTF-8 for ch/paste
  char ctrl = 0;     // 'a'..'z' for ctrl
  bool alt = false;
};

class Terminal {
 public:
  Terminal();
  ~Terminal();
  bool ok() const { return ok_; }
  Size size() const;
  // Wait up to timeout_ms for a key (-1 = forever). KeyType::none on timeout.
  Key read(int timeout_ms);
  void write(const std::string& s);
  // Mouse wheel reporting (SGR). Off by default: while on, selecting text needs Shift in most terminals.
  void enable_mouse(bool on);
  void flush();

 private:
  bool ok_ = false;
  bool mouse_ = false;
  std::string out_;
  std::string pending_;
  Key parse();
};

}  // namespace shaman::tui
