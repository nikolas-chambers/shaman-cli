#include <filesystem>
#include <fstream>

#include "shaman/permission/permission.hpp"
#include "shaman/tool/builtin/edit.hpp"
#include "shaman/tool/tool.hpp"
#include "test.hpp"

using namespace shaman;
namespace fs = std::filesystem;

TEST(edit_exact_unique) {
  auto r = tool::apply_edit("a\nb\nc\n", "b", "B", false);
  CHECK(r && *r == "a\nB\nc\n");
}

TEST(edit_ambiguous_rejected) {
  CHECK(!tool::apply_edit("x x", "x", "y", false));
  auto all = tool::apply_edit("x x", "x", "y", true);
  CHECK(all && *all == "y y");
}

TEST(edit_whitespace_tolerant) {
  auto r = tool::apply_edit("int f() {\n    return 1;\n}\n", "int f() {\n  return 1;\n}", "int f() {\n  return 2;\n}", false);
  CHECK(r.has_value());
  CHECK(r && r->find("return 2;") != std::string::npos);
}

TEST(edit_missing) { CHECK(!tool::apply_edit("abc", "zzz", "y", false)); }

TEST(permission_layers) {
  auto rules = permission::Rules::defaults();
  CHECK(rules.evaluate("read", "/x") == permission::Action::allow);
  CHECK(rules.evaluate("bash", "git status") == permission::Action::allow);
  CHECK(rules.evaluate("bash", "git push") == permission::Action::ask);
  CHECK(rules.evaluate("bash", "rm -rf /home") == permission::Action::deny);
  rules.push(permission::Rules::from_json({{"bash", {{"git push*", "allow"}}}, {"edit", "deny"}}));
  CHECK(rules.evaluate("bash", "git push origin") == permission::Action::allow);
  CHECK(rules.evaluate("edit", "a.cpp") == permission::Action::deny);
  CHECK(rules.evaluate("bash", "make") == permission::Action::ask);  // falls through to defaults
}

TEST(gate_remembers_always) {
  int asked = 0;
  permission::Gate gate(permission::Rules::defaults(), [&](const permission::Request&) {
    ++asked;
    return permission::Reply::always;
  });
  CHECK(gate.check({"bash", "make", ""}));
  CHECK(gate.check({"bash", "make", ""}));
  CHECK_EQ(asked, 1);
}

TEST(builtin_tools_roundtrip) {
  auto root = fs::temp_directory_path() / "shaman-test-tools";
  fs::remove_all(root);
  fs::create_directories(root / "src");
  std::ofstream(root / "src" / "a.cpp") << "int main() {\n  return 0;\n}\n";

  tool::Registry reg;
  tool::register_builtins(reg);
  permission::Gate gate(permission::Rules::from_json({{"*", "allow"}}), nullptr);
  std::set<fs::path> read;
  tool::Context ctx{root, "ses_test", &gate, nullptr, &read, nullptr, nullptr};

  auto edit_before_read = reg.find("edit")->run({{"filePath", "src/a.cpp"}, {"oldString", "0"}, {"newString", "1"}}, ctx);
  CHECK(edit_before_read.is_error);

  auto r = reg.find("read")->run({{"filePath", "src/a.cpp"}}, ctx);
  CHECK(!r.is_error && r.text.find("return 0;") != std::string::npos);
  auto e = reg.find("edit")->run({{"filePath", "src/a.cpp"}, {"oldString", "return 0;"}, {"newString", "return 1;"}}, ctx);
  CHECK(!e.is_error);
  auto g = reg.find("grep")->run({{"pattern", "return 1"}}, ctx);
  CHECK(g.text.find("src/a.cpp:2") != std::string::npos);
  auto gl = reg.find("glob")->run({{"pattern", "**/*.cpp"}}, ctx);
  CHECK(gl.text.find("src/a.cpp") != std::string::npos);
  auto escape = reg.find("read")->run({{"filePath", "../../etc/hostname"}}, ctx);  // external_directory allowed by "*"
  (void)escape;
#ifndef _WIN32
  auto b = reg.find("bash")->run({{"command", "echo hi && exit 3"}}, ctx);
  CHECK(b.is_error && b.text.find("hi") != std::string::npos && b.text.find("exit code 3") != std::string::npos);
#endif
  fs::remove_all(root);
}
