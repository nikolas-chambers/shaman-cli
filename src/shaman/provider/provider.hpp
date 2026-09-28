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

// Wire protocol spoken by an endpoint. Only OpenAI-style chat completions is
// implemented today; the enum is where Anthropic Messages, OpenAI Responses
// and Gemini slot in when key-based providers are added.
enum class Api { openai_chat };

struct ModelInfo {
  std::string id;
  std::string name;
  int64_t context = 128'000;
  int64_t output = 16'384;
  double cost_input = 0;   // USD per 1M tokens
  double cost_output = 0;
  bool free() const { return cost_input == 0 && cost_output == 0; }
};

struct ProviderInfo {
  std::string id;
  std::string name;
  Api api = Api::openai_chat;
  std::string base_url;
  std::vector<std::string> env;           // env vars that may hold an API key
  std::optional<std::string> public_key;  // key usable without signing up (free tier)
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

std::unique_ptr<Provider> make(Api api, Endpoint endpoint);

}  // namespace shaman::provider
