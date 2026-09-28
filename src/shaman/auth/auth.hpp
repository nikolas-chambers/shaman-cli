#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"
#include "shaman/core/result.hpp"

namespace shaman::auth {

// API keys saved with `shaman auth login`. The secret goes to the OS keychain when there is one (macOS
// Keychain, Windows Credential Manager, Secret Service) and auth.json only records that it exists; otherwise,
// and always in portable mode, auth.json holds the key itself (mode 0600). Environment variables and config
// take precedence over both.
class Store {
 public:
  explicit Store(std::filesystem::path file, bool use_keychain = default_keychain());
  static std::filesystem::path default_path();
  static bool default_keychain();  // false in portable mode or with SHAMAN_NO_KEYCHAIN set

  std::optional<std::string> key(const std::string& provider) const;
  Result<void> set(const std::string& provider, const std::string& key);
  Result<void> remove(const std::string& provider);
  std::vector<std::string> providers() const;
  bool in_keychain(const std::string& provider) const;
  // Where new keys go ("macOS Keychain", ..., or the file path).
  std::string location() const;
  // Move keys kept in auth.json into the keychain; returns how many moved.
  Result<int> secure();

 private:
  Result<void> save() const;
  std::filesystem::path file_;
  bool keychain_ = false;
  Json data_ = Json::object();
  mutable std::map<std::string, std::string> cache_;  // keychain reads are slow (secret-tool, prompts)
};

}  // namespace shaman::auth
