#include "test.hpp"

int main() {
  int failed_cases = 0;
  for (auto& c : test::cases()) {
    int before = test::failures();
    c.fn();
    bool ok = test::failures() == before;
    failed_cases += !ok;
    std::cout << (ok ? "  ok   " : "  FAIL ") << c.name << "\n";
  }
  std::cout << test::cases().size() - failed_cases << "/" << test::cases().size() << " passed\n";
  return failed_cases ? 1 : 0;
}
