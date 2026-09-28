// Windows process layer test, runnable under Wine:
//   x86_64-w64-mingw32-g++ -std=c++23 -static -Isrc tests/win/process_test.cpp src/shaman/core/process_win.cpp \
//     src/shaman/core/strings.cpp -o process_test.exe && wine process_test.exe
#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include "shaman/core/process.hpp"

using namespace shaman;
static int failures = 0;
#define EXPECT(cond, what)                                              \
  do {                                                                  \
    bool ok_ = (cond);                                                  \
    std::cout << (ok_ ? "  ok   " : "  FAIL ") << what << std::endl;    \
    if (!ok_) ++failures;                                               \
  } while (0)

static std::string self() {
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return buf;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--echo") {  // child mode: echo stdin lines
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      std::cout << "echo: " << line << std::endl;
      if (line == "quit") break;
    }
    return 3;
  }
  using namespace std::chrono_literals;
  auto r = process::run({"cmd.exe", "/C", "echo hello"}, {});
  EXPECT(r && r->exit_code == 0 && r->output.find("hello") != std::string::npos, "run captures output");

  process::Options in;
  in.input = "apple\nbanana\n";
  r = process::run({self(), "--echo"}, in);
  EXPECT(r && r->output.find("echo: banana") != std::string::npos && r->exit_code == 3, "stdin input and exit code");

  process::Options env;
  env.env = {{"SHAMAN_TEST_VAR", "from-env"}};
  r = process::run({"cmd.exe", "/C", "echo %SHAMAN_TEST_VAR%"}, env);
  EXPECT(r && r->output.find("from-env") != std::string::npos, "extra environment");

  process::Options err;
  err.separate_stderr = true;
  r = process::run({"cmd.exe", "/C", "echo out & echo bad 1>&2"}, err);
  EXPECT(r && r->output.find("out") != std::string::npos && r->error.find("bad") != std::string::npos &&
             r->output.find("bad") == std::string::npos, "separate stderr");

  process::Options quick;
  quick.timeout = 500ms;
  r = process::run({"cmd.exe", "/C", "ping -n 5 127.0.0.1 >nul"}, quick);
  EXPECT(r && r->timed_out, "timeout kills the command");

  auto child = process::Child::spawn({self(), "--echo"});
  EXPECT(bool(child), "spawn piped child");
  if (child) {
    child->write_line("hi there");
    auto line = child->read_line(5000ms);
    EXPECT(line && *line && **line == "echo: hi there", "child round trip (read_line)");
    child->write_line("abc");
    auto exact = child->read_exact(9, 5000ms);
    EXPECT(exact && *exact && **exact == "echo: abc", "read_exact");
    child->read_line(1000ms);  // the rest of that line (Windows text mode ends it with \r\n)
    auto none = child->read_line(200ms);
    EXPECT(none && !*none, "read_line times out without data");
    child->write_line("quit");
    child->read_line(2000ms);
    auto closed = child->read_line(3000ms);
    EXPECT(!closed, "read_line reports a closed child");
  }

  auto bg = process::Background::start("echo started & ping -n 2 127.0.0.1 >nul & echo finished", {});
  EXPECT(bool(bg), "background start");
  if (bg) {
    EXPECT((*bg)->running(), "background running");
    std::string all;
    for (int i = 0; i < 100 && (*bg)->running(); ++i) all += (*bg)->take_output(), Sleep(100);
    all += (*bg)->take_output();
    EXPECT(all.find("started") != std::string::npos && all.find("finished") != std::string::npos, "background output");
    EXPECT(!(*bg)->running() && (*bg)->exit_code() == 0, "background exit code");
  }

  auto echo = process::Background::start("\"" + self() + "\" --echo", {});
  if (echo) {
    (*echo)->write_input("typed\n");
    std::string out;
    for (int i = 0; i < 50 && out.find("echo: typed") == std::string::npos; ++i) out += (*echo)->take_output(), Sleep(100);
    EXPECT(out.find("echo: typed") != std::string::npos, "background write_input");
    (*echo)->kill();
    EXPECT(!(*echo)->running(), "background kill");
  }

  std::cout << (failures ? "FAILED" : "all passed") << std::endl;
  return failures ? 1 : 0;
}
