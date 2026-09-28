#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace shaman::http {

struct SseEvent {
  std::string event;  // empty means "message"
  std::string data;
};

// Incremental Server-Sent Events parser. Feed arbitrary byte chunks; complete
// events are delivered in order.
class SseParser {
 public:
  using Handler = std::function<void(const SseEvent&)>;
  void feed(std::string_view chunk, const Handler& on_event);

 private:
  void line(std::string_view l, const Handler& on_event);
  std::string buf_, event_, data_;
  bool has_data_ = false;
};

}  // namespace shaman::http
