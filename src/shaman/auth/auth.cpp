#include "shaman/auth/auth.hpp"

#include <fstream>

#include "shaman/core/paths.hpp"

namespace shaman::auth {
namespace fs = std::filesystem;

fs::path Store::default_path() { return paths::data_dir() / "auth.json"; }

Store::Store(fs::path file) : file_(std::move(file)) {
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
  auto k = it->value("key", "");
  if (k.empty()) return std::nullopt;
  return k;
}

Result<void> Store::set(const std::string& provider, const std::string& key) {
  data_[provider] = {{"type", "api"}, {"key", key}};
  return save();
}

Result<void> Store::remove(const std::string& provider) {
  data_.erase(provider);
  return save();
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
