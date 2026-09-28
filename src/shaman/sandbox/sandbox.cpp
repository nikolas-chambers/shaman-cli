#include "shaman/sandbox/sandbox.hpp"

#include <cstdlib>

#include "shaman/core/process.hpp"

namespace shaman::sandbox {

namespace fs = std::filesystem;

namespace {
std::string quote(const std::string& s) {  // POSIX single quotes
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

fs::path expand(const std::string& p) {
  if (p.starts_with("~/"))
    if (const char* home = std::getenv("HOME")) return fs::path(home) / p.substr(2);
  return fs::path(p);
}
}  // namespace

Settings from_config(const Json& raw) {
  Settings s;
  auto it = raw.find("sandbox");
  if (it == raw.end()) return s;
  if (it->is_boolean()) {
    s.enabled = it->get<bool>();
    return s;
  }
  if (!it->is_object()) return s;
  const Json& o = *it;
  s.enabled = o.value("enabled", true);
  s.network = o.value("network", true);
  for (auto& w : o.value("writable", std::vector<std::string>{})) s.writable.push_back(expand(w));
  return s;
}

Result<std::string> mechanism() {
#if defined(_WIN32)
  return fail("no sandbox on Windows yet (use WSL, or turn \"sandbox\" off)");
#elif defined(__APPLE__)
  if (!process::which("sandbox-exec")) return fail("sandbox-exec not found");
  return std::string("sandbox-exec");
#else
  if (!process::which("bwrap")) return fail("bubblewrap not installed (apt install bubblewrap / dnf install bubblewrap)");
  return std::string("bubblewrap");
#endif
}

Result<std::string> wrap(const std::string& command, const fs::path& root, const fs::path& cwd, const Settings& s) {
  auto m = mechanism();
  if (!m) return std::unexpected(m.error());
  std::error_code ec;
  auto tmp = fs::temp_directory_path(ec);
#if defined(__APPLE__)
  std::string profile = "(version 1)(allow default)(deny file-write*)(allow file-write* (literal \"/dev/null\") "
                        "(subpath \"/dev/fd\") (subpath \"/private/tmp\") (subpath \"/private/var/folders\")";
  auto allow = [&](const fs::path& p) {
    auto c = fs::weakly_canonical(p, ec);
    profile += " (subpath \"" + (ec ? p : c).string() + "\")";
  };
  allow(root);
  if (!tmp.empty()) allow(tmp);
  for (auto& w : s.writable) allow(w);
  profile += ")";
  if (!s.network) profile += "(deny network*)";
  return "cd " + quote(cwd.string()) + " && sandbox-exec -p " + quote(profile) + " /bin/bash -c " + quote(command);
#else
  std::string cmd = "bwrap --ro-bind / / --dev /dev --proc /proc --die-with-parent";
  auto bind = [&](const fs::path& p) {
    if (fs::exists(p, ec)) cmd += " --bind " + quote(p.string()) + " " + quote(p.string());
  };
  bind(tmp.empty() ? fs::path("/tmp") : tmp);
  if (tmp != "/tmp") bind("/tmp");
  bind(root);
  for (auto& w : s.writable) bind(w);
  if (!s.network) cmd += " --unshare-net";
  std::string sh = process::which("bash") ? "bash" : "sh";
  return cmd + " --chdir " + quote(cwd.string()) + " -- " + sh + " -c " + quote(command);
#endif
}

}  // namespace shaman::sandbox
