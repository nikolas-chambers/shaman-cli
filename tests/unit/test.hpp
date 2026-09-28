#pragma once
// Minimal test harness: TEST(name) { CHECK(expr); CHECK_EQ(a, b); }

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                  \
  static void TEST_CAT(test_, name)();                              \
  static test::Reg TEST_CAT(reg_, name)(#name, TEST_CAT(test_, name)); \
  static void TEST_CAT(test_, name)()
#define CHECK(expr)                                                                          \
  do {                                                                                       \
    if (!(expr)) {                                                                           \
      ++test::failures();                                                                    \
      std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK(" #expr ") failed\n";    \
    }                                                                                        \
  } while (0)
#define CHECK_EQ(a, b)                                                                          \
  do {                                                                                          \
    auto _a = (a); auto _b = (b);                                                               \
    if (!(_a == _b)) {                                                                          \
      ++test::failures();                                                                       \
      std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b "\n    got: " << _a \
                << "\n    want: " << _b << "\n";                                                \
    }                                                                                           \
  } while (0)
