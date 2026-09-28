#pragma once

#include <vector>

#include "shaman/core/result.hpp"
#include "shaman/provider/provider.hpp"

namespace shaman::provider {

// Providers shaman knows about out of the box.
//
// Today that is OpenCode Zen's free tier: a handful of models served at no
// cost by opencode.ai, reachable with the shared key "public" and no sign-up.
// The free lineup rotates, so the built-in list is only a starting point;
// `shaman models --refresh` pulls the live list and caches it.
std::vector<ProviderInfo> builtin();

// Fetch the live Zen model list and keep the free ones. Cached under
// $XDG_CACHE_HOME/shaman/zen-models.json and preferred over the built-ins.
Result<std::vector<ModelInfo>> refresh_zen();
std::optional<std::vector<ModelInfo>> cached_zen();

}  // namespace shaman::provider
