#include "shaman/auth/auth.hpp"

#include <cstdlib>
#include <fstream>

#include "shaman/auth/keychain.hpp"
#include "shaman/core/paths.hpp"

namespace shaman::auth {
namespace fs = std::filesystem;

fs::path Store::default_path() { return paths::data_dir() / "auth.json"; }

bool Store::default_keychain() {
  const char* off = std::getenv("SHAMAN_NO_KEYCHAIN");
  return !paths::portable_root() && !(off && *off) && keychain::backend().has_value();
}

Store::Store(fs::path file, bool use_keychain) : file_(std::move(file)), keychain_(use_keychain) {
  std::ifstream in(file_);
  if (!in) return;
  try {
    data_ = Json::parse(in);
  } catch (...) {
    data_ = Json::object();
  }
}

std::optional<std::string> Store::key(const std::string& provider) const {
  auto it = data_.find(provider);
  if (it == data_.end() || !it->is_object()) return std::nullopt;
  if (it->value("keychain", false)) {
    if (auto c = cache_.find(provider); c != cache_.end()) return c->second;
    auto k = keychain::get(provider);
    if (k) cache_[provider] = *k;
    return k;
  }
  auto k = it->value("key", "");
  if (k.empty()) return std::nullopt;
  return k;
}

Result<void> Store::set(const std::string& provider, const std::string& key) {
  if (keychain_ && keychain::set(provider, key)) {
    data_[provider] = {{"type", "api"}, {"keychain", true}};
    cache_[provider] = key;
  } else {
    data_[provider] = {{"type", "api"}, {"key", key}};
  }
  return save();
}

Result<void> Store::remove(const std::string& provider) {
  if (auto it = data_.find(provider); it != data_.end() && it->is_object() && it->value("keychain", false))
    keychain::erase(provider);
  cache_.erase(provider);
  data_.erase(provider);
  return save();
}

bool Store::in_keychain(const std::string& provider) const {
  auto it = data_.find(provider);
  return it != data_.end() && it->is_object() && it->value("keychain", false);
}

std::string Store::location() const {
  if (keychain_)
    if (auto b = keychain::backend()) return *b;
  return file_.string();
}

Result<int> Store::secure() {
  if (!keychain_) return fail("no OS keychain here (" + location() + " is used)");
  int moved = 0;
  for (auto& [provider, entry] : data_.items()) {
    if (!entry.is_object() || entry.value("keychain", false) || entry.value("key", "").empty()) continue;
    if (auto r = keychain::set(provider, entry.value("key", "")); !r) return std::unexpected(r.error());
    entry = {{"type", "api"}, {"keychain", true}};
    ++moved;
  }
  if (auto r = save(); !r) return std::unexpected(r.error());
  return moved;
}

std::vector<std::string> Store::providers() const {
  std::vector<std::string> out;
  for (auto& [k, _] : data_.items()) out.push_back(k);
  return out;
}

Result<void> Store::save() const {
  std::error_code ec;
  fs::create_directories(file_.parent_path(), ec);
  auto tmp = file_;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << data_.dump(2);
    if (!out) return fail("cannot write " + tmp.string());
  }
  fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
  fs::rename(tmp, file_, ec);
  if (ec) return fail("cannot save " + file_.string() + ": " + ec.message());
  return {};
}

}  // namespace shaman::auth
