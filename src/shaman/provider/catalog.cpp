#include "shaman/provider/catalog.hpp"

#include <cstdlib>
#include <fstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/http/http.hpp"

namespace shaman::provider {
namespace {
namespace fs = std::filesystem;

constexpr const char* kZenUrl = "https://opencode.ai/zen/v1";

std::vector<ModelInfo> zen_free_defaults() {
  // Ordered by preference; the first available one is the default model.
  // Context sizes are conservative; the server enforces the real limits.
  return {
      {"big-pickle", "Big Pickle", 200'000, 32'000},
      {"space-bunny-free", "Space Bunny", 128'000, 16'384},
      {"nemotron-3-ultra-free", "Nemotron 3 Ultra", 128'000, 16'384},
      {"longcat-2.5-preview-free", "LongCat 2.5 Preview", 128'000, 16'384},
      {"mimo-v2.6-flash-free", "MiMo V2.6 Flash", 128'000, 16'384},
      {"mimo-v2.5-free", "MiMo V2.5", 128'000, 16'384},
      {"ling-3.0-flash-fin-free", "Ling 3.0 Flash Fin", 128'000, 16'384},
      {"nemotron-3.5-lightning-free", "Nemotron 3.5 Lightning", 128'000, 16'384},
  };
}

fs::path cache_file() { return paths::cache_dir() / "zen-models.json"; }

bool looks_free(const std::string& id) { return id == "big-pickle" || id.ends_with("-free"); }

}  // namespace

std::vector<ProviderInfo> builtin() {
  ProviderInfo zen;
  zen.id = "opencode";
  zen.name = "OpenCode Zen (free tier)";
  zen.api = Api::openai_chat;
  // SHAMAN_ZEN_URL points at a mock or proxy, handy for debugging and tests.
  const char* url = std::getenv("SHAMAN_ZEN_URL");
  zen.base_url = url && *url ? url : kZenUrl;
  zen.env = {"OPENCODE_API_KEY"};
  zen.public_key = "public";
  zen.models = cached_zen().value_or(zen_free_defaults());
  // Key-based providers (Anthropic, OpenAI, OpenRouter, ...) register here
  // once their Api implementations exist. See docs/ARCHITECTURE.md.
  return {zen};
}

std::optional<std::vector<ModelInfo>> cached_zen() {
  std::ifstream in(cache_file());
  if (!in) return std::nullopt;
  try {
    auto j = Json::parse(in);
    std::vector<ModelInfo> out;
    for (auto& m : j) out.push_back({m.at("id"), m.value("name", m.at("id").get<std::string>())});
    if (out.empty()) return std::nullopt;
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

Result<std::vector<ModelInfo>> refresh_zen() {
  http::Request req;
  req.url = std::string(std::getenv("SHAMAN_ZEN_URL") ? std::getenv("SHAMAN_ZEN_URL") : kZenUrl) + "/models";
  req.timeout_s = 20;
  auto res = http::send(req);
  if (!res) return std::unexpected(res.error());
  if (res->status != 200) return fail("Zen /models returned HTTP " + std::to_string(res->status), int(res->status));

  std::vector<ModelInfo> out;
  auto defaults = zen_free_defaults();
  try {
    for (auto& m : Json::parse(res->body).at("data")) {
      std::string id = m.at("id");
      if (!looks_free(id)) continue;
      ModelInfo info{id, id};
      for (auto& d : defaults)
        if (d.id == id) info = d;
      out.push_back(info);
    }
  } catch (const Json::exception& e) {
    return fail(std::string("unexpected /models payload: ") + e.what());
  }
  log::debug(log::Cat::provider, "zen: {} free models live", out.size());

  Json cache = Json::array();
  for (auto& m : out) cache.push_back({{"id", m.id}, {"name", m.name}});
  std::error_code ec;
  std::filesystem::create_directories(cache_file().parent_path(), ec);
  std::ofstream(cache_file()) << cache.dump(2);
  return out;
}

}  // namespace shaman::provider
