#include "shaman/update/update.hpp"

#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

#include "shaman/core/json.hpp"
#include "shaman/http/http.hpp"
#include "shaman/version.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace shaman::update {
namespace fs = std::filesystem;

std::string asset_name() {
  if (process::termux()) return "shaman-android-termux-aarch64";  // built against Termux's own libraries
#if defined(_WIN32)
  std::string os = "windows";
#elif defined(__APPLE__)
  std::string os = "macos";
#else
  std::string os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  std::string arch = "arm64";
#else
  std::string arch = "x64";
#endif
  return "shaman-" + os + "-" + arch
#ifdef _WIN32
         + ".exe"
#endif
      ;
}

static int compare_versions(const std::string& a, const std::string& b) {
  auto parse = [](std::string s) {
    if (!s.empty() && s[0] == 'v') s.erase(0, 1);
    std::vector<int> v;
    size_t pos = 0;
    while (pos < s.size()) {
      v.push_back(std::atoi(s.c_str() + pos));
      pos = s.find('.', pos);
      if (pos == std::string::npos) break;
      ++pos;
    }
    return v;
  };
  auto va = parse(a), vb = parse(b);
  return va < vb ? -1 : va > vb ? 1 : 0;
}

std::optional<std::string> newer_version(bool enabled) {
  const char* off = std::getenv("SHAMAN_NO_UPDATE_CHECK");
  if (!enabled || (off && *off)) return std::nullopt;
  auto cache = paths::cache_dir() / "update-check.json";
  auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::string latest;
  try {
    std::ifstream in(cache);
    if (in) {
      auto j = Json::parse(in);
      if (now - j.value("checked", int64_t(0)) < 24 * 3600) latest = j.value("latest", "");
    }
  } catch (...) {}
  if (latest.empty()) {
    const char* repo_env = std::getenv("SHAMAN_UPDATE_REPO");
    const char* api_env = std::getenv("SHAMAN_UPDATE_API");
    http::Request req;
    req.url = std::format("{}/repos/{}/releases/latest", api_env && *api_env ? api_env : "https://api.github.com",
                          repo_env && *repo_env ? repo_env : "nikolas-chambers/shaman-cli");
    req.headers = {{"Accept", "application/vnd.github+json"}};
    req.timeout_s = 3;
    auto res = http::send(req);
    if (res && res->status == 200) {
      try {
        latest = Json::parse(res->body).value("tag_name", "");
      } catch (...) {}
    }
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);
    std::ofstream(cache) << Json{{"checked", now}, {"latest", latest}}.dump();  // also caches "none", so offline stays quiet
  }
  if (latest.empty() || compare_versions(latest, kVersion) <= 0) return std::nullopt;
  return latest;
}

int upgrade(const std::string& version, bool check_only) {
  const char* repo_env = std::getenv("SHAMAN_UPDATE_REPO");
  std::string repo = repo_env && *repo_env ? repo_env : "nikolas-chambers/shaman-cli";
  const char* api_env = std::getenv("SHAMAN_UPDATE_API");  // test hook
  std::string api = api_env && *api_env ? api_env : "https://api.github.com";
  http::Request req;
  req.url = version.empty() ? std::format("{}/repos/{}/releases/latest", api, repo)
                            : std::format("{}/repos/{}/releases/tags/{}", api, repo, version.starts_with("v") ? version : "v" + version);
  req.headers = {{"Accept", "application/vnd.github+json"}};
  auto res = http::send(req);
  if (!res) return std::cerr << "upgrade: " << res.error().message << "\n", 1;
  if (res->status != 200) return std::cerr << "upgrade: no release found (HTTP " << res->status << ")\n", 1;
  auto rel = Json::parse(res->body);
  std::string tag = rel.value("tag_name", "");
  if (version.empty() && compare_versions(tag, kVersion) <= 0) return std::cout << "shaman " << kVersion << " is up to date\n", 0;
  if (check_only) return std::cout << "shaman " << tag << " is available (you have " << kVersion << "). Run `shaman upgrade`.\n", 0;

  std::string url;
  for (auto& a : rel.value("assets", Json::array()))
    if (a.value("name", "") == asset_name()) url = a.value("browser_download_url", "");
  if (url.empty()) return std::cerr << "upgrade: release " << tag << " has no " << asset_name() << "\n", 1;

  auto self = paths::executable();  // portable installs keep shaman.ini and data/ beside it, untouched
  if (self.empty()) return std::cerr << "upgrade: cannot locate the running binary\n", 1;
  auto tmp = self;
  tmp += ".new";
  std::cout << "Downloading " << tag << "...\n";
  auto dl = http::send({.url = url, .timeout_s = 600});
  if (!dl || dl->status != 200 || dl->body.size() < 100'000) return std::cerr << "upgrade: download failed\n", 1;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << dl->body;
    if (!out) return std::cerr << "upgrade: cannot write " << tmp.string() << " (try with sudo)\n", 1;
  }
  std::error_code ec;
  fs::permissions(tmp, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                           fs::perms::others_exec, ec);
#ifdef _WIN32
  auto old = self;
  old += ".old";
  fs::rename(self, old, ec);  // Windows can't overwrite a running exe, but can rename it
#endif
  fs::rename(tmp, self, ec);
  if (ec) return std::cerr << "upgrade: cannot replace " << self.string() << ": " << ec.message() << "\n", 1;
  std::cout << "Upgraded to " << tag << "\n";
  return 0;
}

}  // namespace shaman::update
