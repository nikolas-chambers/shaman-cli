#include "shaman/provider/catalog.hpp"

#include <cstdlib>
#include <fstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
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

  // OpenCode Zen (OPENCODE_API_KEY or `shaman auth login opencode`); some of its models cost nothing.
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
  out.push_back(std::move(zen));

  out.push_back(make("anthropic", "Anthropic", Api::anthropic_messages, "https://api.anthropic.com/v1",
                     {"ANTHROPIC_API_KEY"}, "https://console.anthropic.com/settings/keys", anthropic_models()));
  // The Responses API keeps reasoning models' reasoning across tool calls.
  out.push_back(make("openai", "OpenAI", Api::openai_responses, "https://api.openai.com/v1", {"OPENAI_API_KEY"},
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

  // GitHub Models: free (rate-limited) models for any GitHub account. Uses your `gh` login, a token with the
  // Models permission, or GITHUB_TOKEN in GitHub Actions (with `permissions: models: read`).
  {
    auto gh = make("github", "GitHub Models", Api::openai_chat, "https://models.github.ai/inference",
                   {"GITHUB_MODELS_TOKEN", "GITHUB_TOKEN"}, "https://github.com/settings/personal-access-tokens/new",
                   {free_model("openai/gpt-4.1", "GPT-4.1 (GitHub)", 128'000, 16'384),
                    free_model("openai/gpt-4.1-mini", "GPT-4.1 mini (GitHub)", 128'000, 16'384),
                    free_model("deepseek/deepseek-v3-0324", "DeepSeek V3 (GitHub)", 128'000, 8'192),
                    free_model("meta/llama-4-maverick-17b-128e-instruct-fp8", "Llama 4 Maverick (GitHub)", 128'000, 8'192)});
    gh.headers = {{"X-GitHub-Api-Version", "2022-11-28"}};
    if (process::which("gh")) gh.key_command = "gh auth token";
    out.push_back(gh);
  }

  // More OpenAI-compatible services; any model id they serve works (see `shaman models --refresh`).
  struct Compat { const char *id, *name, *base, *env, *url; };
  for (auto& c : std::initializer_list<Compat>{
           {"together", "Together AI", "https://api.together.xyz/v1", "TOGETHER_API_KEY", "https://api.together.ai/settings/api-keys"},
           {"fireworks", "Fireworks AI", "https://api.fireworks.ai/inference/v1", "FIREWORKS_API_KEY", "https://fireworks.ai/account/api-keys"},
           {"cerebras", "Cerebras", "https://api.cerebras.ai/v1", "CEREBRAS_API_KEY", "https://cloud.cerebras.ai"},
           {"deepinfra", "DeepInfra", "https://api.deepinfra.com/v1/openai", "DEEPINFRA_API_KEY", "https://deepinfra.com/dash/api_keys"},
           {"perplexity", "Perplexity", "https://api.perplexity.ai", "PERPLEXITY_API_KEY", "https://www.perplexity.ai/settings/api"},
           {"moonshot", "Moonshot AI (Kimi)", "https://api.moonshot.ai/v1", "MOONSHOT_API_KEY", "https://platform.moonshot.ai/console/api-keys"},
           {"zai", "Z.ai (GLM)", "https://api.z.ai/api/paas/v4", "ZAI_API_KEY", "https://z.ai/manage-apikey/apikey-list"},
           {"nvidia", "NVIDIA NIM", "https://integrate.api.nvidia.com/v1", "NVIDIA_API_KEY", "https://build.nvidia.com"},
           {"huggingface", "Hugging Face", "https://router.huggingface.co/v1", "HF_TOKEN", "https://huggingface.co/settings/tokens"},
           {"sambanova", "SambaNova", "https://api.sambanova.ai/v1", "SAMBANOVA_API_KEY", "https://cloud.sambanova.ai/apis"}})
    out.push_back(make(c.id, c.name, Api::openai_chat, c.base, {c.env}, c.url));

  auto env = [](const char* name) -> std::string { const char* v = std::getenv(name); return v ? v : ""; };
  // Cloud platforms, listed once their environment is set (or configure them under "provider").
  if (auto endpoint = env("AZURE_OPENAI_ENDPOINT"); !endpoint.empty()) {  // https://<resource>.openai.azure.com
    while (endpoint.ends_with('/')) endpoint.pop_back();
    auto azure = make("azure", "Azure OpenAI", Api::openai_responses, endpoint + "/openai/v1", {"AZURE_OPENAI_API_KEY"},
                      "https://portal.azure.com");
    azure.auth_header = "api-key";  // model ids are your deployment names
    out.push_back(azure);
  }
  if (auto project = env("GOOGLE_CLOUD_PROJECT"); !project.empty()) {
    auto loc = env("GOOGLE_CLOUD_LOCATION").empty() ? std::string("global") : env("GOOGLE_CLOUD_LOCATION");
    auto host = loc == "global" ? std::string("aiplatform.googleapis.com") : loc + "-aiplatform.googleapis.com";
    auto vertex = make("vertex", "Google Vertex AI", Api::openai_chat,
                       "https://" + host + "/v1beta1/projects/" + project + "/locations/" + loc + "/endpoints/openapi", {},
                       "https://console.cloud.google.com/vertex-ai",
                       {paid("google/gemini-2.5-pro", "Gemini 2.5 Pro (Vertex)", 1'000'000, 64'000, 1.25, 10),
                        paid("google/gemini-2.5-flash", "Gemini 2.5 Flash (Vertex)", 1'000'000, 64'000, 0.3, 2.5)});
    vertex.key_command = "gcloud auth print-access-token";  // your gcloud login; refreshed every 45 minutes
    out.push_back(vertex);
  }
  if (!env("AWS_BEARER_TOKEN_BEDROCK").empty()) {
    auto region = env("AWS_REGION").empty() ? std::string("us-east-1") : env("AWS_REGION");
    out.push_back(make("bedrock", "Amazon Bedrock", Api::openai_chat, "https://bedrock-runtime." + region + ".amazonaws.com/openai/v1",
                       {"AWS_BEARER_TOKEN_BEDROCK"}, "https://console.aws.amazon.com/bedrock",
                       {paid("openai.gpt-oss-120b-1:0", "gpt-oss 120B (Bedrock)", 128'000, 32'000, 0.15, 0.6)}));
  }

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
      info.is_free = p.id == "opencode" && (id == "big-pickle" || id.ends_with("-free"));
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
