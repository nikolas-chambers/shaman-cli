#include "shaman/command/command.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/github/github.hpp"
#include "shaman/mcp/oauth.hpp"
#include "shaman/provider/anthropic.hpp"
#include "shaman/session/archive.hpp"
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
