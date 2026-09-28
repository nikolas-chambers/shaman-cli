#include "shaman/session/redact.hpp"

#include <regex>

namespace shaman::session {

int redact_secrets(std::string& text) {
  struct Pattern {
    const char* kind;
    std::regex re;
  };
  static const std::vector<Pattern> patterns = [] {
    auto rx = [](const char* s) { return std::regex(s, std::regex::ECMAScript | std::regex::optimize); };
    return std::vector<Pattern>{
        {"private-key", rx(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)")},
        {"aws-key", rx(R"(\b(AKIA|ASIA)[0-9A-Z]{16}\b)")},
        {"github-token", rx(R"(\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}\b|\bgithub_pat_[A-Za-z0-9_]{50,}\b)")},
        {"api-key", rx(R"(\bsk-(ant-|proj-|or-v1-)?[A-Za-z0-9_\-]{20,}\b)")},
        {"google-key", rx(R"(\bAIza[0-9A-Za-z_\-]{35}\b)")},
        {"slack-token", rx(R"(\bxox[baprs]-[A-Za-z0-9\-]{10,}\b)")},
        {"stripe-key", rx(R"(\b(sk|rk)_live_[A-Za-z0-9]{20,}\b)")},
        {"jwt", rx(R"(\beyJ[A-Za-z0-9_\-]{10,}\.eyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\b)")},
        // KEY=value / "password": "value" style assignments of secret-looking names
        {"secret", rx(R"re(((?:password|passwd|secret|api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret)["']?\s*[:=]\s*["']?)([^\s"',;]{8,}))re")},
    };
  }();
  int n = 0;
  for (auto& p : patterns) {
    std::string out;
    auto begin = std::sregex_iterator(text.begin(), text.end(), p.re);
    size_t last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it, ++n) {
      out += text.substr(last, it->position() - last);
      if (std::string(p.kind) == "secret") out += (*it)[1].str() + "[REDACTED:secret]";  // keep the key name
      else out += std::string("[REDACTED:") + p.kind + "]";
      last = it->position() + it->length();
    }
    if (last > 0) text = out + text.substr(last);
  }
  return n;
}

}  // namespace shaman::session
