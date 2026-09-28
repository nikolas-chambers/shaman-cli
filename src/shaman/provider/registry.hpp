#pragma once

#include <memory>
#include <string>
#include <vector>

#include "shaman/auth/auth.hpp"
#include "shaman/config/config.hpp"
#include "shaman/provider/provider.hpp"

namespace shaman::provider {

struct Resolved {
  ProviderInfo provider;
  ModelInfo model;
  Endpoint endpoint;
  Api api = Api::openai_chat;
  std::string ref() const { return provider.id + "/" + model.id; }
  std::unique_ptr<Provider> connect() const { return make(api, endpoint, model.output); }
};

struct KeyInfo {
  std::string key;
  std::string source;  // "env NAME", "auth", "config", "public", "none (local)", ""
};

// Built-in providers merged with config `provider` entries, plus key lookup:
// config apiKey > saved auth > environment > the provider's public key.
class Registry {
 public:
  Registry(const Config& config, const auth::Store& auth);

  const std::vector<ProviderInfo>& providers() const { return providers_; }
  const ProviderInfo* find(const std::string& id) const;

  KeyInfo key(const ProviderInfo& p) const;
  bool usable(const ProviderInfo& p) const;
  bool free_only(const ProviderInfo& p) const;  // only the public key is available
  std::vector<ModelInfo> visible_models(const ProviderInfo& p) const;

  // "provider/model", or a bare model id when unambiguous.
  Result<Resolved> resolve(std::string_view ref) const;
  // Config `model`, $SHAMAN_MODEL, else the first free model.
  Result<Resolved> default_model() const;
  // Free models to try after `ref` fails, in preference order.
  std::vector<std::string> free_fallbacks(std::string_view ref) const;

 private:
  void apply_config(const Json& providers);
  const Config& config_;
  const auth::Store& auth_;
  std::vector<ProviderInfo> providers_;
  std::map<std::string, std::string> config_keys_;
};

}  // namespace shaman::provider
