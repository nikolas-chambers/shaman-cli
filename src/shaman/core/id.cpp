#include "shaman/core/id.hpp"

#include <chrono>
#include <format>
#include <random>

namespace shaman {

int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string make_id(std::string_view prefix) {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return std::format("{}_{:012x}{:08x}", prefix, now_ms(), rng());
}

}  // namespace shaman
