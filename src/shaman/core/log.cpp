#include "shaman/core/log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>

#include "shaman/core/id.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::log {
namespace {

struct State {
  std::mutex mu;
  unsigned mask = 0;
  std::ofstream file;
  std::ofstream trace;
  int64_t start = now_ms();
};

State& state() {
  static State s;
  return s;
}

constexpr std::pair<std::string_view, Cat> kNames[] = {
    {"config", Cat::config}, {"provider", Cat::provider},     {"http", Cat::http},
    {"sse", Cat::sse},       {"tool", Cat::tool},             {"permission", Cat::permission},
    {"session", Cat::session}, {"agent", Cat::agent},         {"mcp", Cat::mcp},
};

}  // namespace

unsigned parse_categories(std::string_view spec) {
  unsigned mask = 0;
  for (auto& raw : str::split(spec, ',')) {
    auto part = str::lower(str::trim(raw));
    if (part == "all" || part == "1" || part == "true" || part == "*") return unsigned(Cat::all);
    for (auto& [n, c] : kNames)
      if (n == part) mask |= unsigned(c);
  }
  return mask;
}

std::string_view name(Cat c) {
  for (auto& [n, cat] : kNames)
    if (cat == c) return n;
  return "all";
}

void enable(unsigned mask) { state().mask |= mask; }
bool enabled(Cat c) { return state().mask & unsigned(c); }

void set_file(const std::filesystem::path& path) {
  auto& s = state();
  std::lock_guard lock(s.mu);
  s.file.open(path, std::ios::app);
}

void write(Cat c, std::string_view message) {
  auto& s = state();
  std::lock_guard lock(s.mu);
  auto line = std::format("[{:>7.3f}s {:<10}] {}\n", (now_ms() - s.start) / 1000.0, name(c), message);
  if (s.file.is_open()) {
    s.file << line;
    s.file.flush();
  } else {
    line.pop_back();
    std::cerr << "\x1b[2m" << line << "\x1b[0m\n";
  }
}

void warn(std::string_view message) {
  std::lock_guard lock(state().mu);
  std::cerr << "\x1b[33mwarning:\x1b[0m " << message << '\n';
}

void trace_open(const std::filesystem::path& path) {
  auto& s = state();
  std::lock_guard lock(s.mu);
  s.trace.open(path, std::ios::app);
}

bool tracing() { return state().trace.is_open(); }

void trace(std::string_view kind, Json data) {
  auto& s = state();
  if (!s.trace.is_open()) return;
  std::lock_guard lock(s.mu);
  Json line = {{"t", now_ms()}, {"kind", kind}, {"data", std::move(data)}};
  s.trace << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
  s.trace.flush();
}

}  // namespace shaman::log
