#include "shaman/provider/openai_chat.hpp"

#include "shaman/core/log.hpp"
#include "shaman/http/http.hpp"
#include "shaman/http/sse.hpp"

namespace shaman::provider {

using namespace llm;


Json openai_chat_body(const ChatRequest& req) {
  Json messages = Json::array();
  if (!req.system.empty()) messages.push_back({{"role", "system"}, {"content", req.system}});
  for (auto& m : req.messages) {
    if (m.role == Role::user) {
      std::string text;
      Json content = Json::array();
      for (auto& p : m.parts) {
        if (auto* r = std::get_if<ToolResultPart>(&p))
          messages.push_back({{"role", "tool"}, {"tool_call_id", r->call_id}, {"content", r->output}});
        else if (auto* t = std::get_if<TextPart>(&p)) text += t->text;
        else if (auto* i = std::get_if<ImagePart>(&p))
          content.push_back({{"type", "image_url"},
                             {"image_url", {{"url", "data:" + i->media_type + ";base64," + i->data}}}});
      }
      if (!content.empty()) {  // multimodal: text first, then images
        if (!text.empty()) content.insert(content.begin(), Json{{"type", "text"}, {"text", text}});
        messages.push_back({{"role", "user"}, {"content", content}});
      } else if (!text.empty()) {
        messages.push_back({{"role", "user"}, {"content", text}});
      }
      continue;
    }
    Json msg = {{"role", "assistant"}};
    auto text = m.text();
    msg["content"] = text.empty() ? Json(nullptr) : Json(text);
    Json calls = Json::array();
    for (auto& c : m.tool_calls()) {
      Json call = {{"id", c.id}, {"type", "function"}, {"function", {{"name", c.name}, {"arguments", c.input.dump()}}}};
      if (c.meta.is_object() && c.meta.contains("extra_content")) call["extra_content"] = c.meta["extra_content"];
      calls.push_back(call);
    }
    if (!calls.empty()) msg["tool_calls"] = calls;
    messages.push_back(msg);
  }

  Json body = {{"model", req.model}, {"messages", messages}, {"stream", true},
               {"stream_options", {{"include_usage", true}}}};
  if (!req.tools.empty()) {
    Json tools = Json::array();
    for (auto& t : req.tools)
      tools.push_back({{"type", "function"},
                       {"function", {{"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}});
    body["tools"] = tools;
  }
  if (req.temperature) body["temperature"] = *req.temperature;
  if (req.max_tokens > 0) body["max_tokens"] = req.max_tokens;
  return body;
}

void OpenAIChatDecoder::feed(const Json& chunk, const EventSink& sink) {
  if (auto u = chunk.find("usage"); u != chunk.end() && u->is_object()) {
    Usage usage;
    usage.input = u->value("prompt_tokens", 0);
    usage.output = u->value("completion_tokens", 0);
    if (auto d = u->find("prompt_tokens_details"); d != u->end() && d->is_object())
      usage.cache_read = d->value("cached_tokens", 0);
    if (auto d = u->find("completion_tokens_details"); d != u->end() && d->is_object())
      usage.reasoning = d->value("reasoning_tokens", 0);
    usage_ = usage;
  }
  auto choices = chunk.find("choices");
  if (choices == chunk.end() || !choices->is_array() || choices->empty()) return;
  const Json& choice = (*choices)[0];
  if (auto d = choice.find("delta"); d != choice.end() && d->is_object()) {
    for (auto key : {"reasoning_content", "reasoning"})
      if (auto r = d->find(key); r != d->end() && r->is_string() && !r->get<std::string>().empty())
        sink(ReasoningDelta{r->get<std::string>()});
    if (auto c = d->find("content"); c != d->end() && c->is_string() && !c->get<std::string>().empty())
      sink(TextDelta{c->get<std::string>()});
    if (auto tcs = d->find("tool_calls"); tcs != d->end() && tcs->is_array()) {
      for (auto& tc : *tcs) {
        auto& call = calls_[tc.value("index", 0)];
        if (auto id = tc.find("id"); id != tc.end() && id->is_string()) call.id = *id;
        if (auto ex = tc.find("extra_content"); ex != tc.end() && ex->is_object()) call.extra = *ex;
        if (auto f = tc.find("function"); f != tc.end()) {
          if (auto n = f->find("name"); n != f->end() && n->is_string()) call.name += n->get<std::string>();
          if (auto a = f->find("arguments"); a != f->end() && a->is_string()) call.args += a->get<std::string>();
        }
      }
    }
  }
  if (auto fr = choice.find("finish_reason"); fr != choice.end() && fr->is_string()) {
    std::string r = *fr;
    finish_ = r == "stop" ? Finish::stop
            : r == "tool_calls" || r == "function_call" ? Finish::tool_calls
            : r == "length" ? Finish::length
            : r == "content_filter" ? Finish::content_filter : Finish::unknown;
  }
}

void OpenAIChatDecoder::finish(const EventSink& sink) {
  for (auto& [index, c] : calls_) {
    Json input = Json::object();
    if (!c.args.empty()) {
      try {
        input = Json::parse(c.args);
      } catch (...) {
        // Surface malformed arguments to the tool layer, which reports the
        // error back to the model instead of crashing the turn.
        input = {{"__invalid_json", c.args}};
      }
    }
    Json meta = c.extra.is_null() ? Json() : Json{{"extra_content", c.extra}};
    sink(ToolCallEvent{{c.id.empty() ? "call_" + std::to_string(index) : c.id, c.name, input, meta}});
  }
  if (!calls_.empty() && finish_ != Finish::length) finish_ = Finish::tool_calls;
  calls_.clear();
  if (usage_) sink(UsageEvent{*usage_});
  sink(FinishEvent{finish_});
}

static std::string error_message(const http::Response& res) {
  try {
    auto j = Json::parse(res.body);
    if (auto e = j.find("error"); e != j.end()) {
      if (e->is_string()) return *e;
      if (e->is_object()) return e->value("message", e->dump());
    }
  } catch (...) {
  }
  return res.body.substr(0, 500);
}

Result<void> OpenAIChatProvider::stream(const ChatRequest& req, const EventSink& sink) {
  Json body = openai_chat_body(req);
  log::trace("request", {{"url", endpoint_.base_url + "/chat/completions"}, {"body", body}});

  http::Request hr;
  hr.method = "POST";
  hr.url = endpoint_.base_url + "/chat/completions";
  hr.body = body.dump();
  hr.cancel = req.cancel;
  hr.headers = {{"Content-Type", "application/json"}, {"Accept", "text/event-stream"}};
  if (!endpoint_.api_key.empty()) hr.headers.emplace_back("Authorization", "Bearer " + endpoint_.api_key);
  for (auto& [k, v] : endpoint_.headers) hr.headers.emplace_back(k, v);

  OpenAIChatDecoder decoder;
  http::SseParser sse;
  std::optional<Error> stream_error;
  auto res = http::stream(hr, [&](std::string_view chunk) {
    sse.feed(chunk, [&](const http::SseEvent& ev) {
      if (ev.data == "[DONE]") return;
      try {
        auto j = Json::parse(ev.data);
        log::trace("chunk", j);
        if (auto e = j.find("error"); e != j.end()) {
          stream_error = Error{e->is_object() ? e->value("message", e->dump()) : e->dump(), 0, true};
          return;
        }
        decoder.feed(j, sink);
      } catch (const Json::exception& e) {
        log::debug(log::Cat::provider, "bad chunk ({}): {}", e.what(), ev.data);
      }
    });
    return !stream_error;
  });
  if (!res) return std::unexpected(res.error());
  if (res->status >= 300) {
    bool retryable = res->status == 429 || res->status == 408 || res->status >= 500;
    return fail(std::format("HTTP {}: {}", res->status, error_message(*res)), int(res->status), retryable);
  }
  if (stream_error) return std::unexpected(*stream_error);
  decoder.finish(sink);
  return {};
}

}  // namespace shaman::provider
