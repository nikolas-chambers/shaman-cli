#include "shaman/provider/registry.hpp"

#include <cstdlib>

#include "shaman/core/log.hpp"
#include "shaman/provider/catalog.hpp"

namespace shaman::provider {

Registry::Registry(const Config& config) : config_(config), providers_(builtin()) {}

std::string Registry::key_for(const ProviderInfo& p) const {
  for (auto& var : p.env)
    if (const char* v = std::getenv(var.c_str()); v && *v) return v;
  return p.public_key.value_or("");
}

Result<Resolved> Registry::resolve(std::string_view ref) const {
  std::string pid, mid(ref);
  if (auto slash = ref.find('/'); slash != std::string_view::npos) {
    pid = ref.substr(0, slash);
    mid = ref.substr(slash + 1);
  }
  for (auto& p : providers_) {
    if (!pid.empty() && p.id != pid) continue;
    for (auto& m : p.models) {
      if (m.id != mid) continue;
      Resolved r{p, m, {p.base_url, key_for(p), {}}};
      log::debug(log::Cat::provider, "resolved {} -> {}", ref, r.endpoint.base_url);
      return r;
    }
    if (!pid.empty() && p.public_key && !key_for(p).empty() && key_for(p) != *p.public_key) {
      // With a real key, models outside the free list are allowed through.
      return Resolved{p, {mid, mid}, {p.base_url, key_for(p), {}}};
    }
  }
  return fail("unknown model '" + std::string(ref) + "' (see `shaman models`)");
}

Result<Resolved> Registry::default_model() const {
  if (!config_.model.empty()) return resolve(config_.model);
  if (const char* env = std::getenv("SHAMAN_MODEL"); env && *env) return resolve(env);
  for (auto& p : providers_)
    if (!p.models.empty()) return resolve(p.id + "/" + p.models.front().id);
  return fail("no models available");
}

std::vector<std::string> Registry::free_fallbacks(std::string_view ref) const {
  std::vector<std::string> out;
  for (auto& p : providers_)
    for (auto& m : p.models)
      if (m.free() && p.id + "/" + m.id != ref) out.push_back(p.id + "/" + m.id);
  return out;
}

}  // namespace shaman::provider
