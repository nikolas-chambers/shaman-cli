#include "shaman/cli/args.hpp"
#include "shaman/config/config.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"
#include "test.hpp"

using namespace shaman;

TEST(wildcard) {
  CHECK(str::wildcard("git status*", "git status --short"));
  CHECK(str::wildcard("*", ""));
  CHECK(!str::wildcard("git log*", "git push"));
  CHECK(str::wildcard("a?c", "abc"));
  CHECK(str::wildcard("*", "**/*.cpp"));  // literal stars in the subject
}

TEST(glob) {
  CHECK(str::glob("**/*.cpp", "src/a/b.cpp"));
  CHECK(str::glob("*.cpp", "src/a/b.cpp"));  // basename match anywhere
  CHECK(str::glob("src/*.hpp", "src/x.hpp"));
  CHECK(!str::glob("src/*.hpp", "src/a/x.hpp"));
  CHECK(str::glob("*.{c,h}pp", "x/y.hpp"));
  CHECK(!str::glob("*.cpp", "main.c"));
}

TEST(within) {
  CHECK(paths::within("/p", "/p/a/b"));
  CHECK(!paths::within("/p", "/p/../etc/passwd"));
  CHECK(!paths::within("/p", "/other"));
}

TEST(jsonc) {
  auto j = parse_jsonc(R"({
    // comment
    "model": "opencode/big-pickle", /* block */
    "list": [1, 2,],
  })");
  CHECK(j.has_value());
  CHECK_EQ((*j)["model"].get<std::string>(), std::string("opencode/big-pickle"));
  CHECK_EQ((*j)["list"].size(), size_t(2));
  auto s = parse_jsonc(R"({"url": "http://x,}"})");  // commas inside strings survive
  CHECK(s.has_value() && (*s)["url"] == "http://x,}");
}

TEST(config_substitution) {
  setenv("SHAMAN_TEST_VAR", "hello", 1);
  Json j = {{"a", "{env:SHAMAN_TEST_VAR} world"}, {"nested", {{"b", "{env:SHAMAN_TEST_UNSET}"}}}};
  substitute(j, "/");
  CHECK_EQ(j["a"].get<std::string>(), std::string("hello world"));
  CHECK_EQ(j["nested"]["b"].get<std::string>(), std::string(""));
}

TEST(args) {
  const char* argv[] = {"shaman", "run", "-m", "opencode/x", "--debug=http", "--yolo", "fix", "it"};
  auto a = cli::parse_args(8, const_cast<char**>(argv), {"yolo", "debug"}, {{"m", "model"}});
  CHECK_EQ(a.positional.size(), size_t(3));
  CHECK_EQ(*a.get("model"), std::string("opencode/x"));
  CHECK_EQ(*a.get("debug"), std::string("http"));
  CHECK(a.has("yolo"));
}
