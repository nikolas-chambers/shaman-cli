#include "shaman/provider/registry.hpp"

#include <cstdlib>

#include "shaman/core/log.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/provider/catalog.hpp"

namespace shaman::provider {

Registry::Registry(const Config& config, const auth::Store& auth) : config_(config), auth_(auth), providers_(builtin()) {
  apply_config(config.provider);
}

// provider.<id>: { name, api, baseURL, apiKey, env, headers, models: { id: { name, context, output, cost, vision } } }
void Registry::apply_config(const Json& providers) {
  for (auto& [id, spec] : providers.items()) {
    if (!spec.is_object()) continue;
    ProviderInfo* p = nullptr;
    for (auto& existing : providers_)
      if (existing.id == id) p = &existing;
    if (!p) {
      providers_.push_back({});
      p = &providers_.back();
      p->id = id;
      p->name = id;
    }
    auto opts = spec.value("options", Json::object());  // opencode-style nesting is accepted too
    auto get = [&](const char* k) { return spec.contains(k) ? spec[k] : opts.value(k, Json()); };
    if (auto v = get("name"); v.is_string()) p->name = v;
    if (auto v = get("api"); v.is_string())
      if (auto api = parse_api(v.get<std::string>())) p->api = *api;
    if (auto v = get("baseURL"); v.is_string()) p->base_url = v;
    if (auto v = get("apiKey"); v.is_string()) config_keys_[id] = v;
    if (auto v = get("env"); v.is_array()) p->env = v.get<std::vector<std::string>>();
    if (auto v = get("keyless"); v.is_boolean()) p->keyless = v;
    if (auto v = get("headers"); v.is_object())
      for (auto& [h, val] : v.items()) p->headers[h] = val.get<std::string>();
    if (auto v = get("body"); v.is_object()) p->body = v;
    if (auto v = get("authHeader"); v.is_string()) p->auth_header = v;
    if (auto v = get("apiKeyCommand"); v.is_string()) p->key_command = v;
    auto models = spec.value("models", Json::object());
    for (auto& [mid, m] : models.items()) {
      if (!m.is_object()) continue;
      // Tweak a known model in place (only the fields given), or add a new one.
      auto it = std::ranges::find_if(p->models, [&](const ModelInfo& x) { return x.id == mid; });
      ModelInfo info = it != p->models.end() ? *it : ModelInfo{mid, mid};
      info.name = m.value("name", info.name);
      if (auto c = m.find("context"); c != m.end()) {  // a number is the window; an object holds tweaks
        if (c->is_number()) info.context = c->get<int64_t>();
        else if (c->is_object()) info.context_options = *c, info.context = c->value("window", info.context);
      }
      info.output = m.value("output", info.output);
      info.vision = m.value("vision", info.vision);
      info.is_free = m.value("free", info.is_free);
      if (auto cost = m.find("cost"); cost != m.end() && cost->is_object()) {
        info.cost_input = cost->value("input", info.cost_input);
        info.cost_output = cost->value("output", info.cost_output);
      }
      if (m.contains("api")) info.api = parse_api(m["api"].get<std::string>());
      if (m.contains("temperature") && m["temperature"].is_number()) info.temperature = m["temperature"].get<double>();
      for (auto k : {"reasoningEffort", "reasoning_effort"})
        if (m.contains(k) && m[k].is_string()) info.reasoning_effort = m[k];
      if (auto b = m.find("body"); b != m.end() && b->is_object()) info.body = *b;
      if (auto t = m.find("tools"); t != m.end() && t->is_object()) info.tools = *t;
      if (it != p->models.end()) *it = info;
      else p->models.push_back(info);
    }
    log::debug(log::Cat::config, "provider {} configured ({})", id, p->base_url);
  }
}

const ProviderInfo* Registry::find(const std::string& id) const {
  for (auto& p : providers_)
    if (p.id == id) return &p;
  return nullptr;
}

KeyInfo Registry::key(const ProviderInfo& p) const {
  if (auto it = config_keys_.find(p.id); it != config_keys_.end() && !it->second.empty()) return {it->second, "config"};
  if (auto k = auth_.key(p.id)) return {*k, auth_.in_keychain(p.id) ? "keychain" : "auth"};
  for (auto& var : p.env)
    if (const char* v = std::getenv(var.c_str()); v && *v) return {v, "env " + var};
  if (!p.key_command.empty()) {
    std::lock_guard lock(command_mu_);
    auto& cached = command_keys_[p.id];
    if (cached.key.empty() || std::chrono::steady_clock::now() - cached.at > std::chrono::minutes(45)) {
      auto r = process::shell(p.key_command, {.timeout = std::chrono::seconds(30)});
      if (r && r->exit_code == 0 && !str::trim(r->output).empty()) cached = {str::trim(r->output), std::chrono::steady_clock::now()};
      else log::debug(log::Cat::provider, "{} apiKeyCommand failed: {}", p.id, r ? r->output.substr(0, 200) : r.error().message);
    }
    if (!cached.key.empty()) return {cached.key, "command"};
  }
  if (p.keyless) return {"", "none (local)"};
  return {"", ""};
}

bool Registry::usable(const ProviderInfo& p) const { return !key(p).source.empty(); }

std::vector<ModelInfo> Registry::visible_models(const ProviderInfo& p) const { return p.models; }

Result<Resolved> Registry::resolve(std::string_view ref) const {
  std::string pid, mid(ref);
  if (auto slash = ref.find('/'); slash != std::string_view::npos) {
    pid = ref.substr(0, slash);
    mid = ref.substr(slash + 1);  // OpenRouter ids contain slashes themselves: keep the rest intact
  }
  auto build = [&](const ProviderInfo& p, const ModelInfo& m) -> Result<Resolved> {
    auto k = key(p);
    if (k.source.empty())
      return fail(std::format("{} needs an API key: set {} or run `shaman auth login {}`", p.name,
                              p.env.empty() ? "one" : p.env.front(), p.id));
    Endpoint e{p.base_url, k.key, p.auth_header, p.headers};
    log::debug(log::Cat::provider, "resolved {}/{} -> {} (key: {})", p.id, m.id, e.base_url, k.source);
    return Resolved{p, m, e, m.api.value_or(p.api)};
  };

  for (auto& p : providers_) {
    if (!pid.empty() && p.id != pid) continue;
    for (auto& m : visible_models(p))
      if (m.id == mid) return build(p, m);
    if (!pid.empty()) {
      if (p.any_model) return build(p, ModelInfo{mid, mid});  // unknown id: pass through with defaults
    }
  }
  return fail("unknown model '" + std::string(ref) + "' (see `shaman models`)");
}

Result<Resolved> Registry::default_model() const {
  if (!config_.model.empty()) return resolve(config_.model);
  if (const char* env = std::getenv("SHAMAN_MODEL"); env && *env) return resolve(env);
  // Prefer a model that costs nothing, then the first model of the first provider with a key.
  for (auto& p : providers_)
    if (usable(p))
      for (auto& m : p.models)
        if (m.free()) return resolve(p.id + "/" + m.id);
  for (auto& p : providers_)
    if (usable(p) && !p.keyless && !p.models.empty()) return resolve(p.id + "/" + p.models.front().id);
  for (auto& p : providers_)
    if (p.keyless && !p.models.empty()) return resolve(p.id + "/" + p.models.front().id);
  return fail("no model provider is set up. Add a key with `shaman auth login <provider>` (e.g. opencode, anthropic, "
              "openai, google, openrouter) or set OPENCODE_API_KEY, ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY, "
              "...; or start Ollama or LM Studio and run `shaman models --refresh`. `shaman models` shows what is available.");
}

std::vector<std::string> Registry::free_fallbacks(std::string_view ref) const {
  std::vector<std::string> out;
  for (auto& p : providers_)
    if (usable(p))
      for (auto& m : p.models)
        if (m.free() && p.id + "/" + m.id != ref) out.push_back(p.id + "/" + m.id);
  return out;
}

}  // namespace shaman::provider
