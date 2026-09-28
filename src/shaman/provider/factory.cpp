#include "shaman/provider/anthropic.hpp"
#include "shaman/provider/openai_chat.hpp"
#include "shaman/provider/openai_responses.hpp"

namespace shaman::provider {

std::string_view to_string(Api api) {
  switch (api) {
    case Api::anthropic_messages: return "anthropic";
    case Api::openai_responses: return "responses";
    default: return "openai";
  }
}

std::optional<Api> parse_api(std::string_view s) {
  if (s == "openai" || s == "openai-compatible" || s == "openai_chat") return Api::openai_chat;
  if (s == "anthropic" || s == "anthropic_messages") return Api::anthropic_messages;
  if (s == "responses" || s == "openai-responses" || s == "openai_responses") return Api::openai_responses;
  return std::nullopt;
}

std::unique_ptr<Provider> make(Api api, Endpoint endpoint, int64_t max_output) {
  switch (api) {
    case Api::openai_chat: return std::make_unique<OpenAIChatProvider>(std::move(endpoint));
    case Api::openai_responses: return std::make_unique<OpenAIResponsesProvider>(std::move(endpoint));
    case Api::anthropic_messages: return std::make_unique<AnthropicProvider>(std::move(endpoint), std::min<int64_t>(max_output, 32'000));
  }
  return nullptr;
}

}  // namespace shaman::provider
