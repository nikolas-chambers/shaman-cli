#include "shaman/http/sse.hpp"

#include "shaman/core/log.hpp"

namespace shaman::http {

void SseParser::feed(std::string_view chunk, const Handler& on_event) {
  buf_.append(chunk);
  size_t start = 0, nl;
  while ((nl = buf_.find('\n', start)) != std::string::npos) {
    std::string_view l(buf_.data() + start, nl - start);
    if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
    line(l, on_event);
    start = nl + 1;
  }
  buf_.erase(0, start);
}

void SseParser::line(std::string_view l, const Handler& on_event) {
  if (l.empty()) {
    if (has_data_) {
      log::debug(log::Cat::sse, "{} {}", event_.empty() ? "message" : event_, data_);
      on_event({event_, data_});
    }
    event_.clear(), data_.clear(), has_data_ = false;
    return;
  }
  if (l.front() == ':') return;  // comment / keep-alive
  auto colon = l.find(':');
  std::string_view field = l.substr(0, colon);
  std::string_view value = colon == std::string_view::npos ? "" : l.substr(colon + 1);
  if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
  if (field == "data") {
    if (has_data_) data_ += '\n';
    data_ += value;
    has_data_ = true;
  } else if (field == "event") {
    event_ = value;
  }
}

}  // namespace shaman::http
