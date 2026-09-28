#include "shaman/llm/message.hpp"

namespace shaman::llm {

template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

std::string Message::text() const {
  std::string out;
  for (auto& p : parts)
    if (auto* t = std::get_if<TextPart>(&p)) out += t->text;
  return out;
}

std::vector<ToolCallPart> Message::tool_calls() const {
  std::vector<ToolCallPart> out;
  for (auto& p : parts)
    if (auto* c = std::get_if<ToolCallPart>(&p)) out.push_back(*c);
  return out;
}

bool Message::has_images() const {
  for (auto& p : parts)
    if (std::holds_alternative<ImagePart>(p)) return true;
  return false;
}

Message Message::user(std::string text) { return {Role::user, {TextPart{std::move(text)}}}; }

Json to_json(const Message& m) {
  Json parts = Json::array();
  for (auto& p : m.parts) {
    parts.push_back(std::visit(overloaded{
        [](const TextPart& t) { return Json{{"type", "text"}, {"text", t.text}}; },
        [](const ReasoningPart& r) { return Json{{"type", "reasoning"}, {"text", r.text}}; },
        [](const ImagePart& i) { return Json{{"type", "image"}, {"media_type", i.media_type}, {"data", i.data}}; },
        [](const ToolCallPart& c) {
          return Json{{"type", "tool_call"}, {"id", c.id}, {"name", c.name}, {"input", c.input}};
        },
        [](const ToolResultPart& r) {
          return Json{{"type", "tool_result"}, {"call_id", r.call_id}, {"name", r.name},
                      {"output", r.output}, {"is_error", r.is_error}};
        },
    }, p));
  }
  return {{"role", m.role == Role::user ? "user" : "assistant"}, {"parts", parts}};
}

Message message_from_json(const Json& j) {
  Message m;
  m.role = j.value("role", "user") == "assistant" ? Role::assistant : Role::user;
  for (auto& p : j.value("parts", Json::array())) {
    auto type = p.value("type", "");
    if (type == "text") m.parts.push_back(TextPart{p.value("text", "")});
    else if (type == "reasoning") m.parts.push_back(ReasoningPart{p.value("text", "")});
    else if (type == "image") m.parts.push_back(ImagePart{p.value("media_type", ""), p.value("data", "")});
    else if (type == "tool_call")
      m.parts.push_back(ToolCallPart{p.value("id", ""), p.value("name", ""), p.value("input", Json::object())});
    else if (type == "tool_result")
      m.parts.push_back(ToolResultPart{p.value("call_id", ""), p.value("name", ""), p.value("output", ""),
                                       p.value("is_error", false)});
  }
  return m;
}

std::string_view to_string(Finish f) {
  switch (f) {
    case Finish::stop: return "stop";
    case Finish::tool_calls: return "tool_calls";
    case Finish::length: return "length";
    case Finish::content_filter: return "content_filter";
    default: return "unknown";
  }
}

}  // namespace shaman::llm
