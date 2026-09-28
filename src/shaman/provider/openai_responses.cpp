#include "shaman/provider/openai_responses.hpp"

#include "shaman/core/log.hpp"
#include "shaman/http/http.hpp"
#include "shaman/http/sse.hpp"

namespace shaman::provider {

using namespace llm;

Json openai_responses_body(const ChatRequest& req) {
  Json input = Json::array();
  auto image = [](const ImagePart& i) {
    return Json{{"type", "input_image"}, {"image_url", "data:" + i.media_type + ";base64," + i.data}};
  };
  for (auto& m : req.messages) {
    if (m.role == Role::user) {
      Json content = Json::array(), tool_images = Json::array();
      for (auto& p : m.parts) {
        if (auto* r = std::get_if<ToolResultPart>(&p)) {
          input.push_back({{"type", "function_call_output"}, {"call_id", r->call_id}, {"output", r->output}});
          for (auto& i : r->images) tool_images.push_back(image(i));
        } else if (auto* t = std::get_if<TextPart>(&p)) {
          if (!t->text.empty()) content.push_back({{"type", "input_text"}, {"text", t->text}});
        } else if (auto* i = std::get_if<ImagePart>(&p)) {
          content.push_back(image(*i));
        }
      }
      if (!tool_images.empty()) {
        tool_images.insert(tool_images.begin(), Json{{"type", "input_text"}, {"text", "Images returned by the tool calls above:"}});
        input.push_back({{"role", "user"}, {"content", tool_images}});
      }
      if (!content.empty()) input.push_back({{"role", "user"}, {"content", content}});
      continue;
    }
    for (auto& p : m.parts) {
      if (auto* r = std::get_if<ReasoningPart>(&p)) {
        if (r->meta.is_object() && r->meta.value("type", "") == "reasoning") input.push_back(r->meta);  // verbatim
      } else if (auto* t = std::get_if<TextPart>(&p)) {
        if (!t->text.empty())
          input.push_back({{"type", "message"}, {"role", "assistant"}, {"content", Json::array({{{"type", "output_text"}, {"text", t->text}}})}});
      } else if (auto* c = std::get_if<ToolCallPart>(&p)) {
        input.push_back({{"type", "function_call"}, {"call_id", c->id}, {"name", c->name}, {"arguments", c->input.dump()}});
      }
    }
  }
  Json body = {{"model", req.model}, {"input", input}, {"stream", true}, {"store", false}};
  if (!req.system.empty()) body["instructions"] = req.system;
  if (!req.tools.empty()) {
    Json tools = Json::array();
    for (auto& t : req.tools)
      tools.push_back({{"type", "function"}, {"name", t.name}, {"description", t.description}, {"parameters", t.parameters}});
    body["tools"] = tools;
  }
  if (!req.reasoning_effort.empty()) {
    body["reasoning"] = {{"effort", req.reasoning_effort}, {"summary", "auto"}};
    body["include"] = Json::array({"reasoning.encrypted_content"});  // so reasoning survives store=false
  } else if (req.temperature) {
    body["temperature"] = *req.temperature;
  }
  if (req.max_tokens > 0) body["max_output_tokens"] = req.max_tokens;
  if (req.extra_body.is_object()) body.merge_patch(req.extra_body);
  return body;
}

void OpenAIResponsesDecoder::feed(const Json& e, const EventSink& sink) {
  auto type = e.value("type", "");
  if (type == "response.output_text.delta") {
    if (auto d = e.value("delta", ""); !d.empty()) sink(TextDelta{d});
  } else if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta") {
    if (auto d = e.value("delta", ""); !d.empty()) sink(ReasoningDelta{d});
  } else if (type == "response.output_item.added") {
    auto item = e.value("item", Json::object());
    if (item.value("type", "") == "function_call")
      calls_[item.value("id", "")] = {item.value("call_id", ""), item.value("name", ""), item.value("arguments", "")};
  } else if (type == "response.function_call_arguments.delta") {
    calls_[e.value("item_id", "")].args += e.value("delta", "");
  } else if (type == "response.output_item.done") {
    auto item = e.value("item", Json::object());
    auto itype = item.value("type", "");
    if (itype == "reasoning") {
      sink(ReasoningDone{"", item});
    } else if (itype == "function_call") {
      auto& pending = calls_[item.value("id", "")];
      std::string args = item.contains("arguments") ? item.value("arguments", "") : pending.args;
      Json input = Json::object();
      if (!args.empty()) {
        try {
          input = Json::parse(args);
        } catch (...) {
          input = {{"__invalid_json", args}};
        }
      }
      auto call_id = item.value("call_id", pending.call_id);
      sink(ToolCallEvent{{call_id.empty() ? item.value("id", "call") : call_id, item.value("name", pending.name), input}});
      any_call_ = true;
    }
  } else if (type == "response.completed" || type == "response.incomplete") {
    auto resp = e.value("response", Json::object());
    auto u = resp.value("usage", Json::object());
    Usage usage;
    usage.input = u.value("input_tokens", 0);
    usage.output = u.value("output_tokens", 0);
    usage.cache_read = u.value("input_tokens_details", Json::object()).value("cached_tokens", 0);
    usage.reasoning = u.value("output_tokens_details", Json::object()).value("reasoning_tokens", 0);
    sink(UsageEvent{usage});
    sink(FinishEvent{type == "response.incomplete" ? Finish::length : any_call_ ? Finish::tool_calls : Finish::stop});
    done_ = true;
  } else if (type == "response.failed" || type == "error") {
    auto err = type == "error" ? e : e.value("response", Json::object()).value("error", Json::object());
    auto code = err.value("code", "");
    error_ = Error{err.value("message", "response failed"), 0, code == "rate_limit_exceeded" || code == "server_error"};
  }
}

static std::string error_message(const http::Response& res) {
  try {
    auto j = Json::parse(res.body);
    if (auto e = j.find("error"); e != j.end()) return e->is_object() ? e->value("message", e->dump()) : e->dump();
  } catch (...) {
  }
  return res.body.substr(0, 500);
}

Result<void> OpenAIResponsesProvider::stream(const ChatRequest& req, const EventSink& sink) {
  Json body = openai_responses_body(req);
  log::trace("request", {{"url", endpoint_.base_url + "/responses"}, {"body", body}});
  http::Request hr;
  hr.method = "POST";
  hr.url = endpoint_.base_url + "/responses";
  hr.body = body.dump();
  hr.cancel = req.cancel;
  hr.headers = {{"Content-Type", "application/json"}, {"Accept", "text/event-stream"}};
  if (!endpoint_.api_key.empty()) {
    if (endpoint_.auth_header.empty()) hr.headers.emplace_back("Authorization", "Bearer " + endpoint_.api_key);
    else hr.headers.emplace_back(endpoint_.auth_header, endpoint_.api_key);
  }
  for (auto& [k, v] : endpoint_.headers) hr.headers.emplace_back(k, v);

  OpenAIResponsesDecoder decoder;
  http::SseParser sse;
  auto res = http::stream(hr, [&](std::string_view chunk) {
    sse.feed(chunk, [&](const http::SseEvent& ev) {
      try {
        auto j = Json::parse(ev.data);
        log::trace("chunk", j);
        decoder.feed(j, sink);
      } catch (const Json::exception& e) {
        log::debug(log::Cat::provider, "bad event ({}): {}", e.what(), ev.data);
      }
    });
    return !decoder.error();
  });
  if (!res) return std::unexpected(res.error());
  if (res->status >= 300) {
    bool retryable = res->status == 429 || res->status == 408 || res->status >= 500;
    return fail(std::format("HTTP {}: {}", res->status, error_message(*res)), int(res->status), retryable);
  }
  if (auto e = decoder.error()) return std::unexpected(*e);
  if (!decoder.done()) return fail("stream ended before the response completed", 0, true);
  return {};
}

}  // namespace shaman::provider
