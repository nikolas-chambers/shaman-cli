#include "shaman/provider/registry.hpp"

#include <cstdlib>

#include "shaman/core/log.hpp"
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
    auto models = spec.value("models", Json::object());
    for (auto& [mid, m] : models.items()) {
      ModelInfo info{mid, m.value("name", mid)};
      info.context = m.value("context", info.context);
      info.output = m.value("output", info.output);
      info.vision = m.value("vision", false);
      info.is_free = m.value("free", false);
      auto cost = m.value("cost", Json::object());
      info.cost_input = cost.value("input", 0.0);
      info.cost_output = cost.value("output", 0.0);
      if (m.contains("api")) info.api = parse_api(m["api"].get<std::string>());
      std::erase_if(p->models, [&](const ModelInfo& x) { return x.id == mid; });
      p->models.push_back(info);
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
  if (auto k = auth_.key(p.id)) return {*k, "auth"};
  for (auto& var : p.env)
    if (const char* v = std::getenv(var.c_str()); v && *v) return {v, "env " + var};
  if (p.public_key) return {*p.public_key, "public"};
  if (p.keyless) return {"", "none (local)"};
  return {"", ""};
}

bool Registry::usable(const ProviderInfo& p) const { return !key(p).source.empty(); }

bool Registry::free_only(const ProviderInfo& p) const { return key(p).source == "public"; }

std::vector<ModelInfo> Registry::visible_models(const ProviderInfo& p) const {
  if (!free_only(p)) return p.models;
  std::vector<ModelInfo> out;
  for (auto& m : p.models)
    if (m.free()) out.push_back(m);
  return out;
}

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
    Endpoint e{p.base_url, k.key, p.headers};
    log::debug(log::Cat::provider, "resolved {}/{} -> {} (key: {})", p.id, m.id, e.base_url, k.source);
    return Resolved{p, m, e, m.api.value_or(p.api)};
  };

  for (auto& p : providers_) {
    if (!pid.empty() && p.id != pid) continue;
    for (auto& m : visible_models(p))
      if (m.id == mid) return build(p, m);
    if (!pid.empty()) {
      if (free_only(p)) return fail("'" + mid + "' is not a free model; set OPENCODE_API_KEY to use paid Zen models");
      if (p.any_model) return build(p, ModelInfo{mid, mid});  // unknown id: pass through with defaults
    }
  }
  return fail("unknown model '" + std::string(ref) + "' (see `shaman models`)");
}

Result<Resolved> Registry::default_model() const {
  if (!config_.model.empty()) return resolve(config_.model);
  if (const char* env = std::getenv("SHAMAN_MODEL"); env && *env) return resolve(env);
  for (auto& p : providers_)
    for (auto& m : visible_models(p))
      if (m.free()) return resolve(p.id + "/" + m.id);
  return fail("no models available");
}

std::vector<std::string> Registry::free_fallbacks(std::string_view ref) const {
  std::vector<std::string> out;
  for (auto& p : providers_)
    for (auto& m : visible_models(p))
      if (m.free() && p.id + "/" + m.id != ref) out.push_back(p.id + "/" + m.id);
  return out;
}

}  // namespace shaman::provider
