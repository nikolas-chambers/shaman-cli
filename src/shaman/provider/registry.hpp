#pragma once

#include <memory>
#include <string>
#include <vector>

#include "shaman/config/config.hpp"
#include "shaman/provider/provider.hpp"

namespace shaman::provider {

struct Resolved {
  ProviderInfo provider;
  ModelInfo model;
  Endpoint endpoint;
  std::string ref() const { return provider.id + "/" + model.id; }
  std::unique_ptr<Provider> connect() const { return make(provider.api, endpoint); }
};

class Registry {
 public:
  explicit Registry(const Config& config);

  const std::vector<ProviderInfo>& providers() const { return providers_; }

  // "provider/model", or a bare model id when unambiguous.
  Result<Resolved> resolve(std::string_view ref) const;

  // Config `model`, else the first free model.
  Result<Resolved> default_model() const;

  // Free models to try after `ref` fails, in preference order.
  std::vector<std::string> free_fallbacks(std::string_view ref) const;

 private:
  std::string key_for(const ProviderInfo& p) const;
  const Config& config_;
  std::vector<ProviderInfo> providers_;
};

}  // namespace shaman::provider
