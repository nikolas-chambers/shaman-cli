#include "shaman/command/command.hpp"
#include "shaman/config/config.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/diff/diff.hpp"
#include "shaman/github/github.hpp"
#include "shaman/index/index.hpp"
#include "shaman/mcp/oauth.hpp"
#include "shaman/provider/anthropic.hpp"
#include "shaman/session/archive.hpp"
#include "shaman/session/runner.hpp"
#include "shaman/tool/builtin/patch.hpp"
#include "shaman/tui/markdown.hpp"
#include "test.hpp"

using namespace shaman;

TEST(anthropic_body_merges_and_caches) {
  llm::ChatRequest req;
  req.model = "claude-test";
  req.system = "sys";
  req.messages.push_back(llm::Message::user("hi"));
  req.messages.push_back(llm::Message::user("again"));  // consecutive user turns merge
  req.messages.push_back({llm::Role::assistant, {llm::ToolCallPart{"t1", "read", {{"filePath", "a"}}}}});
  req.messages.push_back({llm::Role::user, {llm::ToolResultPart{"t1", "read", "data", false}}});
  req.tools.push_back({"read", "Read", {{"type", "object"}}});
  auto b = provider::anthropic_body(req, 8000);
  CHECK_EQ(b["messages"].size(), size_t(3));
  CHECK_EQ(b["messages"][0]["content"].size(), size_t(2));
  CHECK_EQ(b["messages"][2]["content"][0]["type"].get<std::string>(), std::string("tool_result"));
  CHECK(b["system"][0].contains("cache_control"));
  CHECK(b["tools"][0].contains("cache_control"));
  CHECK_EQ(b["max_tokens"].get<int>(), 8000);
}

TEST(anthropic_decoder) {
  provider::AnthropicDecoder d;
  std::string text, thought;
  std::vector<llm::ToolCallPart> calls;
  llm::Finish finish{};
  llm::Usage usage;
  auto sink = [&](const llm::StreamEvent& ev) {
    if (auto* t = std::get_if<llm::TextDelta>(&ev)) text += t->text;
    if (auto* r = std::get_if<llm::ReasoningDelta>(&ev)) thought += r->text;
    if (auto* c = std::get_if<llm::ToolCallEvent>(&ev)) calls.push_back(c->call);
    if (auto* f = std::get_if<llm::FinishEvent>(&ev)) finish = f->reason;
    if (auto* u = std::get_if<llm::UsageEvent>(&ev)) usage = u->usage;
  };
  auto feed = [&](const char* ev, const char* json) { d.feed(ev, Json::parse(json), sink); };
  feed("message_start", R"({"message":{"usage":{"input_tokens":10,"cache_read_input_tokens":90}}})");
  feed("content_block_start", R"({"index":0,"content_block":{"type":"thinking"}})");
  feed("content_block_delta", R"({"index":0,"delta":{"type":"thinking_delta","thinking":"plan"}})");
  feed("content_block_start", R"({"index":1,"content_block":{"type":"text"}})");
  feed("content_block_delta", R"({"index":1,"delta":{"type":"text_delta","text":"ok"}})");
  feed("content_block_start", R"({"index":2,"content_block":{"type":"tool_use","id":"t","name":"grep"}})");
  feed("content_block_delta", R"({"index":2,"delta":{"type":"input_json_delta","partial_json":"{\"pattern\":"}})");
  feed("content_block_delta", R"({"index":2,"delta":{"type":"input_json_delta","partial_json":"\"x\"}"}})");
  feed("content_block_stop", R"({"index":2})");
  feed("message_delta", R"({"delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":7}})");
  feed("message_stop", R"({})");
  CHECK_EQ(text, std::string("ok"));
  CHECK_EQ(thought, std::string("plan"));
  CHECK_EQ(calls.size(), size_t(1));
  CHECK_EQ(calls[0].input["pattern"].get<std::string>(), std::string("x"));
  CHECK(finish == llm::Finish::tool_calls);
  CHECK_EQ(usage.input, int64_t(100));
  CHECK_EQ(usage.cache_read, int64_t(90));
  CHECK_EQ(usage.output, int64_t(7));
}

TEST(patch_parse_and_apply) {
  auto ops = tool::parse_patch(
      "*** Begin Patch\n*** Add File: new.txt\n+one\n*** Update File: a.cpp\n@@ int main() {\n-  return 0;\n+  return 1;\n"
      "*** Delete File: old.txt\n*** End Patch\n");
  CHECK(ops.has_value());
  CHECK_EQ(ops->size(), size_t(3));
  CHECK_EQ((*ops)[0].content, std::string("one\n"));
  auto updated = tool::apply_hunks("int main() {\n  return 0;\n}\n", (*ops)[1].hunks);
  CHECK(updated && *updated == "int main() {\n  return 1;\n}\n");
  CHECK(!tool::parse_patch("no header"));
  CHECK(!tool::apply_hunks("x\n", {{"", {"missing"}, {"y"}}}));
}

TEST(command_expand) {
  command::Command c{"t", "", "fix $1 then $ARGUMENTS", {}, {}, ""};
  CHECK_EQ(command::expand(c, "a b", "/tmp"), std::string("fix a then a b"));
#ifndef _WIN32
  command::Command sh{"s", "", "out: !`echo hi $ARGUMENTS`", {}, {}, ""};
  CHECK_EQ(command::expand(sh, "there", "/tmp"), std::string("out: hi there"));
#endif
}

TEST(pkce_challenge) {
  // Expected value from Python: base64.urlsafe_b64encode(hashlib.sha256(v).digest()).rstrip(b"=")
  CHECK_EQ(mcp::oauth::pkce_challenge("dBjftJeZ4CVP-mJ92K9sgPvRtLVUDAvr_hOm8iKKbTwIrnAaNFgUVmjbtBMlABOELEY"),
           std::string("GKhmfgWQu1QNsRFsldtu3LIRWWjpvoLSbojF-QOzZi8"));
  CHECK(mcp::oauth::random_token() != mcp::oauth::random_token());
}

TEST(github_trigger) {
  CHECK_EQ(github::extract_prompt("please /shaman fix the build", "/shaman"), std::string("fix the build"));
  CHECK_EQ(github::extract_prompt("no trigger here", "/shaman"), std::string(""));
  CHECK_EQ(github::extract_prompt("x/shaman nope", "/shaman"), std::string(""));
  CHECK(github::branch_name("issue", 7, "ses_0123456789abcdef").starts_with("shaman/issue7-"));
}

TEST(archive_roundtrip) {
  session::Info info;
  info.id = "ses_x";
  info.title = "T <b>";
  std::vector<llm::Message> ms{llm::Message::user("hello <world>")};
  auto j = session::export_json(info, ms);
  CHECK_EQ(j["format"].get<std::string>(), std::string("shaman-session"));
  auto html = session::export_html(info, ms);
  CHECK(html.find("hello &lt;world&gt;") != std::string::npos);  // escaped
  CHECK(session::export_markdown(info, ms).find("## User") != std::string::npos);
}

TEST(strings_encoding) {
  CHECK_EQ(str::base64_encode("Man"), std::string("TWFu"));
  CHECK_EQ(str::base64_encode("Ma"), std::string("TWE="));
  CHECK_EQ(str::url_decode(str::url_encode("a b/c?d=é")), std::string("a b/c?d=é"));
  CHECK_EQ(str::html_unescape("&lt;a&gt; &amp; &#65;"), std::string("<a> & A"));
  auto [fields, body] = str::front_matter("---\nname: x\ndescription: \"quoted\"\n---\nbody\n");
  CHECK_EQ(fields.size(), size_t(2));
  CHECK_EQ(fields[1].second, std::string("quoted"));
  CHECK_EQ(body, std::string("body\n"));
}

TEST(tui_wrap_and_markdown) {
  auto lines = tui::wrap("\x1b[1mhello world foo\x1b[0m", 11);
  CHECK_EQ(lines.size(), size_t(2));
  CHECK_EQ(tui::display_width(lines[0]), size_t(11));
  CHECK_EQ(tui::display_width("héllo"), size_t(5));
  auto md = tui::render_markdown("# Title\n- item\n```\ncode\n```", 40);
  CHECK(md.size() >= 4);
}

#include "shaman/extras/schedule.hpp"
#include "shaman/session/redact.hpp"

TEST(redact_secrets) {
  std::string t = "key sk-proj-abcdefghijklmnopqrstuvwx and AKIAABCDEFGHIJKLMNOP\npassword=hunter2hunter2 ok=fine\n"
                  "-----BEGIN RSA PRIVATE KEY-----\nMIIabc\n-----END RSA PRIVATE KEY-----";
  int n = session::redact_secrets(t);
  CHECK(n >= 4);
  CHECK(t.find("sk-proj-") == std::string::npos);
  CHECK(t.find("AKIA") == std::string::npos);
  CHECK(t.find("hunter2") == std::string::npos);
  CHECK(t.find("password=[REDACTED:secret]") != std::string::npos);  // key name kept
  CHECK(t.find("MIIabc") == std::string::npos);
  CHECK(t.find("ok=fine") != std::string::npos);
  std::string clean = "nothing secret here, version=1.2.3";
  CHECK_EQ(session::redact_secrets(clean), 0);
}

TEST(cron_validation) {
  CHECK(extras::valid_cron("0 9 * * 1-5"));
  CHECK(extras::valid_cron("*/15 * * * *"));
  CHECK(extras::valid_cron("@daily"));
  CHECK(extras::valid_cron("0 0 1 jan mon"));
  CHECK(!extras::valid_cron("0 9 * *"));
  CHECK(!extras::valid_cron("rm -rf / * * *"));
}

TEST(tui_markdown_table) {
  auto lines = tui::render_markdown("| Name | Qty |\n|---|---|\n| apple | 3 |\n| kiwi | 12 |", 60);
  CHECK_EQ(lines.size(), size_t(6));  // top, header, separator, 2 rows, bottom
  size_t w = tui::display_width(lines[0]);
  for (auto& l : lines) CHECK_EQ(tui::display_width(l), w);  // aligned
}

TEST(unified_diff_hunks_and_stats) {
  std::string before = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n";
  std::string after = "a\nb\nC\nd\ne\nf\ng\nh\ni\nj\nk\n";
  auto d = diff::unified(before, after, "f.txt", 1);
  CHECK(d.starts_with("--- a/f.txt\n+++ b/f.txt\n"));
  CHECK(d.find("@@ -2,3 +2,3 @@\n b\n-c\n+C\n d\n") != std::string::npos);  // separate hunks with 1 line of context
  CHECK(d.find("+k\n") != std::string::npos);
  auto s = diff::stats(d);
  CHECK_EQ(s.added, 2);
  CHECK_EQ(s.removed, 1);
  CHECK(diff::unified("same\n", "same\n", "x").empty());
  CHECK(diff::unified("", "new\n", "n").find("@@ -0,0 +1,1 @@\n+new\n") != std::string::npos);
}

TEST(session_title_from_first_message) {
  CHECK_EQ(session::make_title("  fix   the build\nmore detail"), std::string("fix the build"));
  CHECK_EQ(session::make_title("\n\nsecond line"), std::string("second line"));
  auto t = session::make_title("Why is the cart total wrong when I apply a ten percent discount to the order?", 40);
  CHECK_EQ(t, std::string("Why is the cart total wrong when I apply…"));
  auto u = session::make_title(std::string(39, 'a') + "éé", 40);  // never splits a code point
  CHECK_EQ(u, std::string(39, 'a') + "…");
}

TEST(portable_ini_layer) {
  auto j = parse_ini("; comment\n[keys]\nopencode = sk-1 ; trailing\nanthropic =\n[settings]\nagent = plan\nmode = acceptEdits\n"
                     "goal_max_rounds = 7\nsnapshot = false\n[permission]\nbash = allow\n[env]\nFOO = \"bar baz\"\n");
  CHECK_EQ(j["provider"]["opencode"]["apiKey"].get<std::string>(), std::string("sk-1"));
  CHECK(!j["provider"].contains("anthropic"));  // empty values are ignored
  CHECK_EQ(j["default_agent"].get<std::string>(), std::string("plan"));
  CHECK_EQ(j["mode"].get<std::string>(), std::string("acceptEdits"));
  CHECK_EQ(j["goal_max_rounds"].get<int>(), 7);
  CHECK(j["snapshot"].is_boolean() && !j["snapshot"].get<bool>());
  CHECK_EQ(j["permission"]["bash"].get<std::string>(), std::string("allow"));
  CHECK_EQ(j["env"]["FOO"].get<std::string>(), std::string("bar baz"));
}

TEST(portable_ini_tui_sections) {
  auto j = parse_ini("[tui]\ntheme = halloween\nmouse = true\n[keybinds]\nctrl+g = /sessions\n");
  CHECK_EQ(j["tui"]["theme"].get<std::string>(), std::string("halloween"));
  CHECK(j["tui"]["mouse"].get<bool>());
  CHECK_EQ(j["tui"]["keybinds"]["ctrl+g"].get<std::string>(), std::string("/sessions"));
}

TEST(symbol_index_scan) {
  auto names = [](const std::vector<index::Symbol>& v) {
    std::string s;
    for (auto& x : v) s += x.kind + ":" + x.name + "@" + std::to_string(x.line) + " ";
    return s;
  };
  auto py = names(index::scan("a.py", "import os\nclass Cart:\n    def total(self):\n        return 1\nasync def main():\n    pass\n"));
  CHECK_EQ(py, std::string("class:Cart@2 function:total@3 function:main@5 "));
  auto ts = names(index::scan("a.ts", "export interface User {}\nexport const load = async (id: string) => {\n}\nexport default class App {}\nfunction helper() {}\n"));
  CHECK_EQ(ts, std::string("type:User@1 function:load@2 class:App@4 function:helper@5 "));
  auto go = names(index::scan("a.go", "package x\ntype Server struct {}\nfunc (s *Server) Start() error {\nfunc New() *Server {\n"));
  CHECK_EQ(go, std::string("type:Server@2 function:Start@3 function:New@4 "));
  auto rs = names(index::scan("a.rs", "pub struct Config {}\nimpl Config {\n    pub fn load() -> Self {\n"));
  CHECK_EQ(rs, std::string("struct:Config@1 impl:Config@2 function:load@3 "));
  auto cpp = names(index::scan("a.cpp", "namespace app {\nclass Widget : public Base {\nint Widget::size() const {\nif (x) {\nstatic void helper(int a) {\n"));
  CHECK_EQ(cpp, std::string("namespace:app@1 class:Widget@2 function:Widget::size@3 function:helper@5 "));
  CHECK(index::scan("notes.txt", "def x():").empty());
}
