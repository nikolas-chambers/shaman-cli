#include "shaman/http/sse.hpp"
#include "shaman/provider/openai_chat.hpp"
#include "test.hpp"

using namespace shaman;

TEST(sse_split_chunks) {
  http::SseParser p;
  std::vector<http::SseEvent> got;
  auto h = [&](const http::SseEvent& e) { got.push_back(e); };
  p.feed("data: {\"a\"", h);
  p.feed(":1}\r\n\r\n: keepalive\n\nevent: x\ndata: one\ndata: two\n", h);
  p.feed("\n", h);
  CHECK_EQ(got.size(), size_t(2));
  CHECK_EQ(got[0].data, std::string("{\"a\":1}"));
  CHECK_EQ(got[1].event, std::string("x"));
  CHECK_EQ(got[1].data, std::string("one\ntwo"));
}

TEST(openai_decoder_text_and_tools) {
  provider::OpenAIChatDecoder d;
  std::string text;
  std::vector<llm::ToolCallPart> calls;
  llm::Finish finish = llm::Finish::unknown;
  llm::Usage usage;
  auto sink = [&](const llm::StreamEvent& ev) {
    if (auto* t = std::get_if<llm::TextDelta>(&ev)) text += t->text;
    if (auto* c = std::get_if<llm::ToolCallEvent>(&ev)) calls.push_back(c->call);
    if (auto* f = std::get_if<llm::FinishEvent>(&ev)) finish = f->reason;
    if (auto* u = std::get_if<llm::UsageEvent>(&ev)) usage = u->usage;
  };
  d.feed(Json::parse(R"({"choices":[{"delta":{"content":"Hel"}}]})"), sink);
  d.feed(Json::parse(R"({"choices":[{"delta":{"content":"lo"}}]})"), sink);
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"c1","function":{"name":"read","arguments":"{\"filePath\":"}}]}}]})"), sink);
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\"a.txt\"}"}}]}}]})"), sink);
  d.feed(Json::parse(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"), sink);
  d.feed(Json::parse(R"({"choices":[],"usage":{"prompt_tokens":10,"completion_tokens":5}})"), sink);
  d.finish(sink);
  CHECK_EQ(text, std::string("Hello"));
  CHECK_EQ(calls.size(), size_t(1));
  CHECK_EQ(calls[0].name, std::string("read"));
  CHECK_EQ(calls[0].input["filePath"].get<std::string>(), std::string("a.txt"));
  CHECK(finish == llm::Finish::tool_calls);
  CHECK_EQ(usage.input, int64_t(10));
}

TEST(openai_decoder_bad_arguments) {
  provider::OpenAIChatDecoder d;
  llm::ToolCallPart call;
  auto sink = [&](const llm::StreamEvent& ev) {
    if (auto* c = std::get_if<llm::ToolCallEvent>(&ev)) call = c->call;
  };
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"x","function":{"name":"bash","arguments":"{oops"}}]}}]})"), sink);
  d.finish(sink);
  CHECK(call.input.contains("__invalid_json"));
}

TEST(openai_body_roundtrip) {
  llm::ChatRequest req;
  req.model = "big-pickle";
  req.system = "sys";
  req.messages.push_back(llm::Message::user("hi"));
  req.messages.push_back({llm::Role::assistant, {llm::TextPart{"ok"}, llm::ToolCallPart{"c1", "read", {{"filePath", "a"}}}}});
  req.messages.push_back({llm::Role::user, {llm::ToolResultPart{"c1", "read", "contents", false}}});
  req.tools.push_back({"read", "Read a file", {{"type", "object"}}});
  auto body = provider::openai_chat_body(req);
  auto& m = body["messages"];
  CHECK_EQ(m.size(), size_t(4));
  CHECK_EQ(m[0]["role"].get<std::string>(), std::string("system"));
  CHECK_EQ(m[2]["tool_calls"][0]["function"]["arguments"].get<std::string>(), std::string("{\"filePath\":\"a\"}"));
  CHECK_EQ(m[3]["role"].get<std::string>(), std::string("tool"));
  CHECK_EQ(m[3]["tool_call_id"].get<std::string>(), std::string("c1"));
  CHECK(body["stream"].get<bool>());
}

TEST(openai_thought_signature_roundtrip) {
  provider::OpenAIChatDecoder d;
  llm::ToolCallPart call;
  auto sink = [&](const llm::StreamEvent& ev) {
    if (auto* c = std::get_if<llm::ToolCallEvent>(&ev)) call = c->call;
  };
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"a","extra_content":{"google":{"thought_signature":"SIG"}},
         "function":{"name":"ls","arguments":"{}"}}]}}]})"), sink);
  d.finish(sink);
  CHECK_EQ(call.meta["extra_content"]["google"]["thought_signature"].get<std::string>(), std::string("SIG"));
  llm::ChatRequest req;
  req.messages.push_back({llm::Role::assistant, {call}});
  auto body = provider::openai_chat_body(req);
  CHECK_EQ(body["messages"][0]["tool_calls"][0]["extra_content"]["google"]["thought_signature"].get<std::string>(), std::string("SIG"));
  CHECK_EQ(llm::message_from_json(llm::to_json(req.messages[0])).tool_calls()[0].meta, call.meta);  // persisted
}

TEST(openai_parallel_calls_same_index) {
  provider::OpenAIChatDecoder d;
  std::vector<llm::ToolCallPart> calls;
  auto sink = [&](const llm::StreamEvent& ev) {
    if (auto* c = std::get_if<llm::ToolCallEvent>(&ev)) calls.push_back(c->call);
  };
  // Gemini-style: each parallel call arrives whole, all with index 0
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"a","function":{"name":"bash","arguments":"{\"command\":\"ls\"}"}}]}}]})"), sink);
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"b","function":{"name":"batch","arguments":"{}"}}]}}]})"), sink);
  d.feed(Json::parse(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"c","function":{"name":"memory","arguments":"{\"action\":\"view\"}"}}]}}]})"), sink);
  d.finish(sink);
  CHECK_EQ(calls.size(), size_t(3));
  CHECK_EQ(calls[1].name, std::string("batch"));
  CHECK_EQ(calls[2].input["action"].get<std::string>(), std::string("view"));
}
