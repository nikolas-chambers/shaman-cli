#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "shaman/core/result.hpp"
#include "shaman/llm/message.hpp"

namespace shaman::provider {

// Wire protocol spoken by an endpoint. OpenAI-style chat completions covers
// most providers (OpenAI, OpenRouter, Groq, DeepSeek, xAI, Gemini's OpenAI
// endpoint, Ollama, LM Studio, ...); Anthropic has its own Messages API.
enum class Api { openai_chat, anthropic_messages };

std::string_view to_string(Api api);
std::optional<Api> parse_api(std::string_view s);

struct ModelInfo {
  std::string id;
  std::string name;
  int64_t context = 128'000;
  int64_t output = 16'384;
  double cost_input = 0;   // USD per 1M tokens
  double cost_output = 0;
  bool vision = false;
  bool is_free = false;    // served at no cost (eligible for the free fallback chain)
  std::optional<Api> api;  // overrides the provider's protocol for this model
  bool free() const { return is_free; }
};

struct ProviderInfo {
  std::string id;
  std::string name;
  Api api = Api::openai_chat;
  std::string base_url;
  std::vector<std::string> env;           // env vars that may hold an API key
  std::optional<std::string> public_key;  // key usable without signing up (free tier)
  bool keyless = false;                   // local servers that need no key at all
  bool any_model = true;                  // accept model ids outside `models` once usable
  std::string key_url;                    // where to get a key, shown by `shaman auth`
  std::map<std::string, std::string> headers;
  std::vector<ModelInfo> models;
};

struct Endpoint {
  std::string base_url;
  std::string api_key;
  std::map<std::string, std::string> headers;
};

using EventSink = std::function<void(const llm::StreamEvent&)>;

class Provider {
 public:
  virtual ~Provider() = default;
  // Stream one completion. Errors carry `retryable` for 429/5xx/network.
  virtual Result<void> stream(const llm::ChatRequest& req, const EventSink& sink) = 0;
};

std::unique_ptr<Provider> make(Api api, Endpoint endpoint, int64_t max_output = 16384);

}  // namespace shaman::provider
