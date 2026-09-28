#pragma once

#include <map>

#include "shaman/provider/provider.hpp"

namespace shaman::provider {

// Translate the neutral request into an OpenAI /chat/completions body.
Json openai_chat_body(const llm::ChatRequest& req);

// Accumulates streamed chat.completion.chunk objects into StreamEvents.
// Separate from the transport so it can be unit tested with canned chunks.
class OpenAIChatDecoder {
 public:
  void feed(const Json& chunk, const EventSink& sink);
  void finish(const EventSink& sink);  // flush tool calls, usage, finish reason

 private:
  struct PendingCall {
    std::string id, name, args;
    Json extra;  // "extra_content" (Gemini thought_signature), returned verbatim next turn
  };
  std::vector<PendingCall> calls_;   // in arrival order
  std::map<int, size_t> by_index_;   // stream index -> latest call with that index
  std::optional<llm::Usage> usage_;
  llm::Finish finish_ = llm::Finish::unknown;
};

class OpenAIChatProvider final : public Provider {
 public:
  explicit OpenAIChatProvider(Endpoint e) : endpoint_(std::move(e)) {}
  Result<void> stream(const llm::ChatRequest& req, const EventSink& sink) override;

 private:
  Endpoint endpoint_;
};

}  // namespace shaman::provider
