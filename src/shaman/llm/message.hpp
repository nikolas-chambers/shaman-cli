#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "shaman/core/json.hpp"

// Provider-neutral conversation model. Providers translate to and from their
// wire formats; everything else in shaman speaks only these types.
namespace shaman::llm {

enum class Role { user, assistant };

struct TextPart { std::string text; };
struct ReasoningPart { std::string text; };
struct ToolCallPart {
  std::string id;
  std::string name;
  Json input = Json::object();
};
struct ToolResultPart {
  std::string call_id;
  std::string name;
  std::string output;
  bool is_error = false;
};

using Part = std::variant<TextPart, ReasoningPart, ToolCallPart, ToolResultPart>;

struct Message {
  Role role = Role::user;
  std::vector<Part> parts;

  std::string text() const;  // concatenated TextParts
  std::vector<ToolCallPart> tool_calls() const;
  static Message user(std::string text);
};

Json to_json(const Message& m);
Message message_from_json(const Json& j);

struct ToolSpec {
  std::string name;
  std::string description;
  Json parameters;  // JSON Schema
};

struct ChatRequest {
  std::string model;
  std::string system;
  std::vector<Message> messages;
  std::vector<ToolSpec> tools;
  std::optional<double> temperature;
  int max_tokens = 0;  // 0 = provider default
  std::atomic<bool>* cancel = nullptr;
};

struct Usage {
  int64_t input = 0;
  int64_t output = 0;
  int64_t cache_read = 0;
  int64_t reasoning = 0;
  int64_t total() const { return input + output; }
};

enum class Finish { stop, tool_calls, length, content_filter, unknown };

// Streaming events, in arrival order. Tool calls are delivered whole, once
// their arguments have finished streaming.
struct TextDelta { std::string text; };
struct ReasoningDelta { std::string text; };
struct ToolCallEvent { ToolCallPart call; };
struct UsageEvent { Usage usage; };
struct FinishEvent { Finish reason; };
using StreamEvent = std::variant<TextDelta, ReasoningDelta, ToolCallEvent, UsageEvent, FinishEvent>;

std::string_view to_string(Finish f);

}  // namespace shaman::llm
