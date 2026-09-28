#pragma once

#include <map>

#include "shaman/provider/provider.hpp"

namespace shaman::provider {

// Translate the neutral request into an Anthropic /v1/messages body.
// Consecutive same-role messages are merged; the system prompt and the last
// tool definition carry cache_control so long sessions hit the prompt cache.
Json anthropic_body(const llm::ChatRequest& req, int64_t default_max_tokens);

// Accumulates Anthropic stream events (message_start, content_block_*,
// message_delta, ...) into StreamEvents.
class AnthropicDecoder {
 public:
  void feed(const std::string& event, const Json& data, const EventSink& sink);
  std::optional<Error> error() const { return error_; }

 private:
  struct Block {
    std::string type, id, name, json;
  };
  std::map<int, Block> blocks_;
  llm::Usage usage_;
  llm::Finish finish_ = llm::Finish::unknown;
  std::optional<Error> error_;
};

class AnthropicProvider final : public Provider {
 public:
  AnthropicProvider(Endpoint e, int64_t max_tokens) : endpoint_(std::move(e)), max_tokens_(max_tokens) {}
  Result<void> stream(const llm::ChatRequest& req, const EventSink& sink) override;

 private:
  Endpoint endpoint_;
  int64_t max_tokens_;
};

}  // namespace shaman::provider
