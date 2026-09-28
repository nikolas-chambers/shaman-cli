#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"
#include "shaman/core/result.hpp"

namespace shaman::auth {

// API keys saved with `shaman auth login`, in $XDG_DATA_HOME/shaman/auth.json
// (mode 0600). Environment variables and config take precedence.
class Store {
 public:
  explicit Store(std::filesystem::path file);
  static std::filesystem::path default_path();

  std::optional<std::string> key(const std::string& provider) const;
  Result<void> set(const std::string& provider, const std::string& key);
  Result<void> remove(const std::string& provider);
  std::vector<std::string> providers() const;

 private:
  Result<void> save() const;
  std::filesystem::path file_;
  Json data_ = Json::object();
};

}  // namespace shaman::auth
