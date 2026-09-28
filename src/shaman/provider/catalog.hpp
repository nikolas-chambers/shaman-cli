#pragma once

#include <vector>

#include "shaman/core/result.hpp"
#include "shaman/provider/provider.hpp"

namespace shaman::provider {

// Providers shaman knows about out of the box.
//
// OpenCode Zen's free tier works with no sign-up: a handful of models served
// at no cost with the shared key "public". Every other provider becomes usable
// once it has a key (env var, `shaman auth login`, or config), or, for local
// servers like Ollama, immediately.
std::vector<ProviderInfo> builtin();

// Fetch a provider's live model list (GET {base}/models) and cache it under
// $XDG_CACHE_HOME/shaman/models/<provider>.json. Known metadata is kept.
Result<std::vector<ModelInfo>> refresh(const ProviderInfo& provider, const std::string& key);
std::optional<std::vector<ModelInfo>> cached(const std::string& provider_id);

// Merge a fetched list into known models, keeping metadata for known ids.
std::vector<ModelInfo> merge_models(const std::vector<ModelInfo>& known, const std::vector<ModelInfo>& live);

}  // namespace shaman::provider
