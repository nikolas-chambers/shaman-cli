#include "shaman/provider/catalog.hpp"

#include <cstdlib>
#include <fstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/http/http.hpp"

namespace shaman::provider {
namespace {
namespace fs = std::filesystem;

ModelInfo free_model(std::string id, std::string name, int64_t context = 128'000, int64_t output = 16'384) {
  ModelInfo m{std::move(id), std::move(name), context, output};
  m.is_free = true;
  return m;
}

ModelInfo paid(std::string id, std::string name, int64_t context, int64_t output, double in, double out,
               bool vision = true, std::optional<Api> api = std::nullopt) {
  ModelInfo m{std::move(id), std::move(name), context, output, in, out};
  m.vision = vision;
  m.api = api;
  return m;
}

std::vector<ModelInfo> anthropic_models(std::optional<Api> api = std::nullopt) {
  return {paid("claude-opus-5-5", "Claude Opus 5.5", 200'000, 32'000, 4, 20, true, api),
          paid("claude-sonnet-5", "Claude Sonnet 5", 200'000, 32'000, 2, 10, true, api),
          paid("claude-haiku-4-5", "Claude Haiku 4.5", 200'000, 32'000, 1, 5, true, api),
          paid("claude-fable-5-1", "Claude Fable 5.1", 200'000, 32'000, 10, 50, true, api)};
}

ProviderInfo make(std::string id, std::string name, Api api, std::string base, std::vector<std::string> env,
                  std::string key_url, std::vector<ModelInfo> models = {}) {
  ProviderInfo p;
  p.id = std::move(id);
  p.name = std::move(name);
  p.api = api;
  p.base_url = std::move(base);
  p.env = std::move(env);
  p.key_url = std::move(key_url);
  p.models = std::move(models);
  return p;
}

// Model lists include embeddings, speech, image and video models; keep chat models only.
bool chat_model(const std::string& id) {
  for (auto word : {"embed", "tts", "whisper", "transcribe", "veo", "lyria", "imagen", "image", "dall-e", "live", "audio",
                    "robotics", "aqa", "moderation", "computer-use", "deep-research", "nano-banana", "translate", "sora"})
    if (id.find(word) != std::string::npos) return false;
  return true;
}

fs::path cache_file(const std::string& id) { return paths::cache_dir() / "models" / (id + ".json"); }

}  // namespace

std::vector<ProviderInfo> builtin() {
  std::vector<ProviderInfo> out;

  // OpenCode Zen. Free models work with the public key; a real key unlocks the rest.
  // SHAMAN_ZEN_URL points at a mock or proxy, handy for debugging and tests.
  const char* zen_url = std::getenv("SHAMAN_ZEN_URL");
  auto zen = make("opencode", "OpenCode Zen", Api::openai_chat, zen_url && *zen_url ? zen_url : "https://opencode.ai/zen/v1",
                  {"OPENCODE_API_KEY"}, "https://opencode.ai/zen",
                  {free_model("big-pickle", "Big Pickle", 200'000, 32'000),
                   free_model("space-bunny-free", "Space Bunny"),
                   free_model("nemotron-3-ultra-free", "Nemotron 3 Ultra"),
                   free_model("longcat-2.5-preview-free", "LongCat 2.5 Preview"),
                   free_model("mimo-v2.6-flash-free", "MiMo V2.6 Flash"),
                   free_model("mimo-v2.5-free", "MiMo V2.5"),
                   free_model("ling-3.0-flash-fin-free", "Ling 3.0 Flash Fin"),
                   free_model("nemotron-3.5-lightning-free", "Nemotron 3.5 Lightning")});
  for (auto& m : anthropic_models(Api::anthropic_messages)) zen.models.push_back(m);
  zen.models.push_back(paid("deepseek-v4-pro", "DeepSeek V4 Pro", 128'000, 16'384, 1.74, 3.48, false));
  zen.models.push_back(paid("qwen3.8-max", "Qwen3.8 Max", 256'000, 32'000, 2, 6, false));
  zen.models.push_back(paid("kimi-k3", "Kimi K3", 256'000, 32'000, 3, 15, false));
  zen.models.push_back(paid("glm-5.3", "GLM 5.3", 200'000, 32'000, 1.4, 4.4, false));
  zen.public_key = "public";
  out.push_back(std::move(zen));

  out.push_back(make("anthropic", "Anthropic", Api::anthropic_messages, "https://api.anthropic.com/v1",
                     {"ANTHROPIC_API_KEY"}, "https://console.anthropic.com/settings/keys", anthropic_models()));
  out.push_back(make("openai", "OpenAI", Api::openai_chat, "https://api.openai.com/v1", {"OPENAI_API_KEY"},
                     "https://platform.openai.com/api-keys",
                     {paid("gpt-5.5", "GPT 5.5", 272'000, 32'000, 5, 30), paid("gpt-5.4-mini", "GPT 5.4 Mini", 272'000, 32'000, 0.75, 4.5)}));
  out.push_back(make("google", "Google Gemini", Api::openai_chat, "https://generativelanguage.googleapis.com/v1beta/openai",
                     {"GEMINI_API_KEY", "GOOGLE_API_KEY"}, "https://aistudio.google.com/apikey",
                     {paid("gemini-3.5-flash", "Gemini 3.5 Flash", 1'000'000, 64'000, 1.5, 9),
                      paid("gemini-3.1-pro-preview", "Gemini 3.1 Pro", 1'000'000, 64'000, 2, 12),
                      paid("gemini-2.5-flash", "Gemini 2.5 Flash", 1'000'000, 64'000, 0.3, 2.5)}));
  out.push_back(make("openrouter", "OpenRouter", Api::openai_chat, "https://openrouter.ai/api/v1", {"OPENROUTER_API_KEY"},
                     "https://openrouter.ai/keys"));
  out.back().headers = {{"HTTP-Referer", "https://github.com/nikolas-chambers/shaman-cli"}, {"X-Title", "shaman-cli"}};
  out.push_back(make("xai", "xAI", Api::openai_chat, "https://api.x.ai/v1", {"XAI_API_KEY"}, "https://console.x.ai",
                     {paid("grok-4.7", "Grok 4.7", 256'000, 32'000, 2, 6)}));
  out.push_back(make("deepseek", "DeepSeek", Api::openai_chat, "https://api.deepseek.com/v1", {"DEEPSEEK_API_KEY"},
                     "https://platform.deepseek.com/api_keys"));
  out.push_back(make("groq", "Groq", Api::openai_chat, "https://api.groq.com/openai/v1", {"GROQ_API_KEY"},
                     "https://console.groq.com/keys"));
  out.push_back(make("mistral", "Mistral", Api::openai_chat, "https://api.mistral.ai/v1", {"MISTRAL_API_KEY"},
                     "https://console.mistral.ai/api-keys"));

  const char* ollama = std::getenv("OLLAMA_HOST");
  auto local = make("ollama", "Ollama (local)", Api::openai_chat,
                    ollama && *ollama ? std::string(ollama) + "/v1" : "http://localhost:11434/v1", {}, "");
  local.keyless = true;
  out.push_back(local);
  auto lms = make("lmstudio", "LM Studio (local)", Api::openai_chat, "http://localhost:1234/v1", {}, "");
  lms.keyless = true;
  out.push_back(lms);

  for (auto& p : out)
    if (auto c = cached(p.id)) p.models = merge_models(p.models, *c);
  return out;
}

std::vector<ModelInfo> merge_models(const std::vector<ModelInfo>& known, const std::vector<ModelInfo>& live) {
  std::vector<ModelInfo> out = known;
  for (auto& m : live) {
    bool found = false;
    for (auto& k : out) found = found || k.id == m.id;
    if (!found) out.push_back(m);
  }
  return out;
}

std::optional<std::vector<ModelInfo>> cached(const std::string& provider_id) {
  std::ifstream in(cache_file(provider_id));
  if (!in) return std::nullopt;
  try {
    std::vector<ModelInfo> out;
    for (auto& m : Json::parse(in)) {
      ModelInfo info{m.at("id"), m.value("name", m.at("id").get<std::string>())};
      info.is_free = m.value("free", false);
      out.push_back(info);
    }
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

Result<std::vector<ModelInfo>> refresh(const ProviderInfo& p, const std::string& key) {
  http::Request req;
  req.url = p.base_url + "/models";
  req.timeout_s = 20;
  if (!key.empty()) {
    req.headers.emplace_back("Authorization", "Bearer " + key);
    if (p.api == Api::anthropic_messages) {
      req.headers.emplace_back("x-api-key", key);
      req.headers.emplace_back("anthropic-version", "2023-06-01");
    }
  }
  auto res = http::send(req);
  if (!res) return std::unexpected(res.error());
  if (res->status != 200) return fail(p.id + " /models returned HTTP " + std::to_string(res->status), int(res->status));

  std::vector<ModelInfo> out;
  try {
    auto payload = Json::parse(res->body);  // named: iterating a member of a temporary would dangle
    for (auto& m : payload.at("data")) {
      std::string id = m.at("id");
      if (id.starts_with("models/")) id = id.substr(7);  // Gemini prefixes ids
      if (!chat_model(id)) continue;
      ModelInfo info{id, m.value("display_name", m.value("name", id))};
      // Zen marks free models by id; everything else is assumed paid.
      info.is_free = p.public_key && (id == "big-pickle" || id.ends_with("-free"));
      out.push_back(info);
    }
  } catch (const Json::exception& e) {
    return fail(std::string("unexpected /models payload: ") + e.what());
  }
  log::debug(log::Cat::provider, "{}: {} models live", p.id, out.size());

  Json cache = Json::array();
  for (auto& m : out) cache.push_back({{"id", m.id}, {"name", m.name}, {"free", m.is_free}});
  std::error_code ec;
  fs::create_directories(cache_file(p.id).parent_path(), ec);
  std::ofstream(cache_file(p.id)) << cache.dump(2);
  return out;
}

}  // namespace shaman::provider
