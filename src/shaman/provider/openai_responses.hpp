#pragma once

#include <map>

#include "shaman/provider/provider.hpp"

namespace shaman::provider {

// OpenAI Responses API (POST /responses): OpenAI's newer protocol, needed for its reasoning models to
// keep their reasoning between tool calls. Stateless: store=false, with encrypted reasoning items
// replayed from the conversation.
Json openai_responses_body(const llm::ChatRequest& req);

class OpenAIResponsesDecoder {
 public:
  void feed(const Json& event, const EventSink& sink);
  bool done() const { return done_; }
  std::optional<Error> error() const { return error_; }

 private:
  struct PendingCall {
    std::string call_id, name, args;
  };
  std::map<std::string, PendingCall> calls_;  // by item id
  bool any_call_ = false, done_ = false;
  std::optional<Error> error_;
};

class OpenAIResponsesProvider final : public Provider {
 public:
  explicit OpenAIResponsesProvider(Endpoint e) : endpoint_(std::move(e)) {}
  Result<void> stream(const llm::ChatRequest& req, const EventSink& sink) override;

 private:
  Endpoint endpoint_;
};

}  // namespace shaman::provider
