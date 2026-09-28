#include "shaman/provider/anthropic.hpp"

#include "shaman/core/log.hpp"
#include "shaman/http/http.hpp"
#include "shaman/http/sse.hpp"

namespace shaman::provider {

using namespace llm;

Json anthropic_body(const ChatRequest& req, int64_t default_max_tokens) {
  Json messages = Json::array();
  auto push = [&](const char* role, Json block) {
    if (!messages.empty() && messages.back()["role"] == role) messages.back()["content"].push_back(std::move(block));
    else messages.push_back({{"role", role}, {"content", Json::array({std::move(block)})}});
  };
  for (auto& m : req.messages) {
    const char* role = m.role == Role::user ? "user" : "assistant";
    for (auto& p : m.parts) {
      if (auto* t = std::get_if<TextPart>(&p)) {
        if (!t->text.empty()) push(role, {{"type", "text"}, {"text", t->text}});
      } else if (auto* i = std::get_if<ImagePart>(&p)) {
        push(role, {{"type", "image"}, {"source", {{"type", "base64"}, {"media_type", i->media_type}, {"data", i->data}}}});
      } else if (auto* c = std::get_if<ToolCallPart>(&p)) {
        push(role, {{"type", "tool_use"}, {"id", c->id}, {"name", c->name}, {"input", c->input}});
      } else if (auto* r = std::get_if<ToolResultPart>(&p)) {
        push(role, {{"type", "tool_result"}, {"tool_use_id", r->call_id}, {"content", r->output}, {"is_error", r->is_error}});
      }
      // Reasoning is not replayed: unsigned thinking blocks are rejected by the API.
    }
  }
  // Cache the conversation prefix: mark the last block of the final message.
  if (!messages.empty() && !messages.back()["content"].empty())
    messages.back()["content"].back()["cache_control"] = {{"type", "ephemeral"}};

  Json body = {{"model", req.model}, {"messages", messages}, {"stream", true},
               {"max_tokens", req.max_tokens > 0 ? req.max_tokens : default_max_tokens}};
  if (!req.system.empty())
    body["system"] = Json::array({{{"type", "text"}, {"text", req.system}, {"cache_control", {{"type", "ephemeral"}}}}});
  if (!req.tools.empty()) {
    Json tools = Json::array();
    for (auto& t : req.tools) tools.push_back({{"name", t.name}, {"description", t.description}, {"input_schema", t.parameters}});
    tools.back()["cache_control"] = {{"type", "ephemeral"}};
    body["tools"] = tools;
  }
  if (req.temperature) body["temperature"] = *req.temperature;
  return body;
}

void AnthropicDecoder::feed(const std::string& event, const Json& d, const EventSink& sink) {
  auto type = d.value("type", event);
  if (type == "message_start") {
    auto u = d["message"].value("usage", Json::object());
    usage_.input = u.value("input_tokens", 0) + u.value("cache_read_input_tokens", 0) + u.value("cache_creation_input_tokens", 0);
    usage_.cache_read = u.value("cache_read_input_tokens", 0);
    usage_.output = u.value("output_tokens", 0);
  } else if (type == "content_block_start") {
    auto& cb = d["content_block"];
    blocks_[d.value("index", 0)] = {cb.value("type", ""), cb.value("id", ""), cb.value("name", ""), ""};
  } else if (type == "content_block_delta") {
    auto& delta = d["delta"];
    auto dt = delta.value("type", "");
    if (dt == "text_delta") sink(TextDelta{delta.value("text", "")});
    else if (dt == "thinking_delta") sink(ReasoningDelta{delta.value("thinking", "")});
    else if (dt == "input_json_delta") blocks_[d.value("index", 0)].json += delta.value("partial_json", "");
  } else if (type == "content_block_stop") {
    auto it = blocks_.find(d.value("index", 0));
    if (it != blocks_.end() && it->second.type == "tool_use") {
      Json input = Json::object();
      if (!it->second.json.empty()) {
        try {
          input = Json::parse(it->second.json);
        } catch (...) {
          input = {{"__invalid_json", it->second.json}};
        }
      }
      sink(ToolCallEvent{{it->second.id, it->second.name, input}});
    }
  } else if (type == "message_delta") {
    auto reason = d["delta"].value("stop_reason", "");
    finish_ = reason == "end_turn" || reason == "stop_sequence" ? Finish::stop
            : reason == "tool_use" ? Finish::tool_calls
            : reason == "max_tokens" ? Finish::length
            : reason == "refusal" ? Finish::content_filter : Finish::unknown;
    if (auto u = d.find("usage"); u != d.end()) usage_.output = u->value("output_tokens", usage_.output);
  } else if (type == "message_stop") {
    sink(UsageEvent{usage_});
    sink(FinishEvent{finish_});
  } else if (type == "error") {
    auto e = d.value("error", Json::object());
    auto etype = e.value("type", "");
    error_ = Error{e.value("message", "stream error"), 0, etype == "overloaded_error" || etype == "api_error" || etype == "rate_limit_error"};
  }
}

Result<void> AnthropicProvider::stream(const ChatRequest& req, const EventSink& sink) {
  Json body = anthropic_body(req, max_tokens_);
  log::trace("request", {{"url", endpoint_.base_url + "/messages"}, {"body", body}});
  http::Request hr;
  hr.method = "POST";
  hr.url = endpoint_.base_url + "/messages";
  hr.body = body.dump();
  hr.cancel = req.cancel;
  hr.headers = {{"Content-Type", "application/json"}, {"Accept", "text/event-stream"}, {"anthropic-version", "2023-06-01"}};
  if (!endpoint_.api_key.empty()) {
    hr.headers.emplace_back("x-api-key", endpoint_.api_key);
    hr.headers.emplace_back("Authorization", "Bearer " + endpoint_.api_key);  // gateways such as Zen
  }
  for (auto& [k, v] : endpoint_.headers) hr.headers.emplace_back(k, v);

  AnthropicDecoder decoder;
  http::SseParser sse;
  auto res = http::stream(hr, [&](std::string_view chunk) {
    sse.feed(chunk, [&](const http::SseEvent& ev) {
      try {
        auto j = Json::parse(ev.data);
        log::trace("chunk", j);
        decoder.feed(ev.event, j, sink);
      } catch (const Json::exception& e) {
        log::debug(log::Cat::provider, "bad event ({}): {}", e.what(), ev.data);
      }
    });
    return !decoder.error();
  });
  if (!res) return std::unexpected(res.error());
  if (res->status >= 300) {
    std::string msg = res->body.substr(0, 500);
    try {
      msg = Json::parse(res->body)["error"].value("message", msg);
    } catch (...) {
    }
    bool retryable = res->status == 429 || res->status == 408 || res->status == 529 || res->status >= 500;
    return fail(std::format("HTTP {}: {}", res->status, msg), int(res->status), retryable);
  }
  if (auto e = decoder.error()) return std::unexpected(*e);
  return {};
}

}  // namespace shaman::provider
