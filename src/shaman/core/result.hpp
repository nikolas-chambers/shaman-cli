#pragma once

#include <expected>
#include <string>
#include <utility>

namespace shaman {

struct Error {
  std::string message;
  int code = 0;            // HTTP status or errno, 0 if not applicable
  bool retryable = false;  // transient: rate limit, 5xx, network
};

template <class T = void>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(std::string message, int code = 0, bool retryable = false) {
  return std::unexpected(Error{std::move(message), code, retryable});
}

}  // namespace shaman
