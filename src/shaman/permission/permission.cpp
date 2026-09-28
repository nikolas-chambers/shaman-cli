#include "shaman/permission/permission.hpp"

#include "shaman/core/log.hpp"
#include "shaman/core/strings.hpp"

#include <cstring>

namespace shaman::permission {

std::string_view to_string(Action a) {
  return a == Action::allow ? "allow" : a == Action::deny ? "deny" : "ask";
}

static Action parse(const std::string& s) {
  return s == "allow" ? Action::allow : s == "deny" ? Action::deny : Action::ask;
}

Rules Rules::defaults() {
  // Reading is free; changing things asks. Read-only shell commands are
  // pre-approved so exploration doesn't drown you in prompts.
  return from_json({
      {"read", "allow"}, {"list", "allow"}, {"glob", "allow"}, {"grep", "allow"},
      {"todo", "allow"}, {"task", "allow"}, {"skill", "allow"}, {"lsp", "allow"}, {"websearch", "allow"},
      {"memory", "allow"}, {"http", {{"*", "ask"}, {"GET http://localhost*", "allow"}, {"GET http://127.0.0.1*", "allow"}}},
      {"edit", "ask"}, {"webfetch", "ask"}, {"external_directory", "ask"}, {"mcp", "ask"}, {"plugin", "ask"},
      {"doom_loop", "ask"},
      {"bash", {
          {"*", "ask"},
          {"ls*", "allow"}, {"pwd", "allow"}, {"cat *", "allow"}, {"head *", "allow"},
          {"tail *", "allow"}, {"wc *", "allow"}, {"which *", "allow"}, {"echo *", "allow"},
          {"rg *", "allow"}, {"grep *", "allow"}, {"find *", "allow"}, {"tree*", "allow"},
          {"git status*", "allow"}, {"git diff*", "allow"}, {"git log*", "allow"},
          {"git show*", "allow"}, {"git branch", "allow"},
          {"rm -rf /*", "deny"}, {"sudo *", "ask"},
      }},
  });
}

Rules Rules::from_json(const Json& j) {
  Rules r;
  if (!j.is_object()) return r;
  for (auto& [perm, value] : j.items()) {
    if (value.is_string()) r.rules_.push_back({perm, "*", parse(value), 0});
    else if (value.is_object())
      for (auto& [pattern, action] : value.items())
        if (action.is_string()) r.rules_.push_back({perm, pattern, parse(action), 0});
  }
  r.layers_ = 1;
  return r;
}

void Rules::push(const Rules& layer) {
  for (auto rule : layer.rules_) {
    rule.layer += layers_;
    rules_.push_back(rule);
  }
  layers_ += std::max(layer.layers_, 1);
}

Action Rules::evaluate(std::string_view permission, std::string_view subject) const {
  const Rule* best = nullptr;
  auto specificity = [](const std::string& p) { return p.size() - str::count(p, "*"); };
  for (auto& rule : rules_) {
    if (rule.permission != permission && rule.permission != "*") continue;
    if (!str::wildcard(rule.pattern, subject)) continue;
    if (!best || rule.layer > best->layer ||
        (rule.layer == best->layer && specificity(rule.pattern) >= specificity(best->pattern)))
      best = &rule;
  }
  Action a = best ? best->action : Action::ask;
  log::debug(log::Cat::permission, "{} '{}' -> {} (rule: {})", permission, subject, to_string(a),
             best ? best->permission + ":" + best->pattern : "none");
  return a;
}

ShellAnalysis analyze_shell(const std::string& cmd) {
  ShellAnalysis a;
  std::string cur;
  auto push = [&] {
    auto t = str::trim(cur);
    if (!t.empty()) a.commands.push_back(t);
    cur.clear();
  };
  char quote = 0;
  for (size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (quote) {
      if (c == '\\' && quote == '"' && i + 1 < cmd.size()) {
        cur += c;
        cur += cmd[++i];
        continue;
      }
      if (quote == '"' && (c == '`' || (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '('))) a.substitution = true;
      if (c == quote) quote = 0;
      cur += c;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      cur += c;
      continue;
    }
    if (c == '\\' && i + 1 < cmd.size()) {
      cur += c;
      cur += cmd[++i];
      continue;
    }
    if (c == '`' || (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '(') ||
        ((c == '<' || c == '>') && i + 1 < cmd.size() && cmd[i + 1] == '('))
      a.substitution = true;
    if (c == '<' && i + 1 < cmd.size() && cmd[i + 1] == '<' && !(i + 2 < cmd.size() && cmd[i + 2] == '<')) {
      // heredoc: skip its body (data, not commands); keep the rest of the line
      size_t j = i + 2;
      if (j < cmd.size() && cmd[j] == '-') ++j;
      while (j < cmd.size() && std::isspace(static_cast<unsigned char>(cmd[j])) && cmd[j] != '\n') ++j;
      size_t start = j;
      while (j < cmd.size() && !std::isspace(static_cast<unsigned char>(cmd[j])) && std::string_view(";&|<>").find(cmd[j]) == std::string_view::npos) ++j;
      std::string delim = cmd.substr(start, j - start);
      std::erase_if(delim, [](char ch) { return ch == '\'' || ch == '"' || ch == '\\'; });
      auto eol = cmd.find('\n', j);
      // The rest of the heredoc's first line is still shell (e.g. `> file && cat file`).
      auto rest = analyze_shell(cmd.substr(j, (eol == std::string::npos ? cmd.size() : eol) - j));
      a.writes = a.writes || rest.writes;
      a.substitution = a.substitution || rest.substitution;
      cur += cmd.substr(i, j - i);
      if (!rest.commands.empty()) {
        cur += " " + rest.commands.front();
        push();
        a.commands.insert(a.commands.end(), rest.commands.begin() + 1, rest.commands.end());
      }
      if (eol == std::string::npos) break;
      size_t pos = eol + 1;  // find the terminator line
      while (pos < cmd.size()) {
        auto next = cmd.find('\n', pos);
        auto line = str::trim(cmd.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
        pos = next == std::string::npos ? cmd.size() : next + 1;
        if (line == delim) break;
      }
      i = pos - 1;
      push();
      continue;
    }
    if (c == '>') {
      // Output redirection. Harmless forms: >/dev/null, 2>&1, >&2.
      size_t j = i + 1;
      if (j < cmd.size() && (cmd[j] == '>' || cmd[j] == '|')) ++j;
      if (j < cmd.size() && cmd[j] == '&') {
        cur += cmd.substr(i, j + 1 - i);
        i = j;
        continue;  // >&2 / 2>&1
      }
      while (j < cmd.size() && cmd[j] == ' ') ++j;
      // the null device: /dev/null, or nul / $null on Windows (cmd, PowerShell)
      auto word_end = cmd.find_first_of(" \t;&|)", j);
      auto target = str::lower(cmd.substr(j, word_end == std::string::npos ? std::string::npos : word_end - j));
      if (target != "/dev/null" && target != "nul" && target != "$null") a.writes = true;
      cur += c;
      continue;
    }
    if (c == '&' && i + 1 < cmd.size() && cmd[i + 1] == '>') {  // &> file
      a.writes = true;
      cur += c;
      continue;
    }
    if (c == ';' || c == '\n' || c == '|' || c == '&') {
      push();
      if (i + 1 < cmd.size() && (cmd[i + 1] == '|' || cmd[i + 1] == '&') && c != ';' && c != '\n') ++i;
      continue;
    }
    cur += c;
  }
  push();
  for (auto& c : a.commands)
    if (c.starts_with("tee ") || c == "tee" || (c.starts_with("sed") && c.find(" -i") != std::string::npos)) a.writes = true;
  return a;
}

Action Rules::evaluate_shell(std::string_view permission, const std::string& command) const {
  auto full = evaluate(permission, command);
  if (full == Action::deny) return Action::deny;
  auto a = analyze_shell(command);
  Action worst = a.commands.empty() ? full : Action::allow;
  for (auto& c : a.commands) {
    auto r = evaluate(permission, c);
    if (r == Action::deny) return Action::deny;
    // Wrappers must not dodge deny rules: `sudo rm -rf /` is checked as `rm -rf /` too.
    std::string inner = c;
    for (bool again = true; again;) {
      again = false;
      for (auto w : {"sudo ", "env ", "nohup ", "time ", "command ", "exec "})
        if (inner.starts_with(w)) inner = str::trim(inner.substr(std::strlen(w))), again = true;
    }
    if (inner != c && evaluate(permission, inner) == Action::deny) return Action::deny;
    if (r == Action::ask) worst = Action::ask;
  }
  if ((a.writes || a.substitution) && worst == Action::allow) {
    log::debug(log::Cat::permission, "{} '{}': writes or substitution; asking", permission, command);
    worst = Action::ask;
  }
  return worst;
}

Json Rules::to_json() const {
  Json out = Json::array();
  for (auto& r : rules_)
    out.push_back({{"layer", r.layer}, {"permission", r.permission}, {"pattern", r.pattern},
                   {"action", to_string(r.action)}});
  return out;
}

const char* to_string(Mode m) {
  switch (m) {
    case Mode::normal: return "default";
    case Mode::accept_edits: return "acceptEdits";
    case Mode::yolo: return "yolo";
  }
  return "default";
}

std::optional<Mode> parse_mode(std::string_view s) {
  if (s == "default" || s == "normal") return Mode::normal;
  if (s == "acceptEdits" || s == "accept-edits" || s == "accept_edits") return Mode::accept_edits;
  if (s == "yolo" || s == "bypassPermissions") return Mode::yolo;
  return std::nullopt;
}

bool Gate::check(const Request& req) {
  if (hook_)
    if (auto a = hook_(req)) {
      log::debug(log::Cat::permission, "{} '{}' -> {} (plugin)", req.permission, req.subject, to_string(*a));
      if (*a != Action::ask) return *a == Action::allow;
    }
  auto action = req.permission == "bash" ? rules_.evaluate_shell(req.permission, req.subject) : rules_.evaluate(req.permission, req.subject);
  switch (action) {
    case Action::allow: return true;
    case Action::deny:
      log::trace("permission", {{"permission", req.permission}, {"subject", req.subject}, {"result", "deny"}});
      return false;
    case Action::ask:
      if (mode() == Mode::yolo) return true;
      if (mode() == Mode::accept_edits && req.permission == "edit") return true;
      break;
  }
  if (always_.contains({req.permission, req.subject}) || always_.contains({req.permission, "*"})) return true;
  Reply reply = asker_ ? asker_(req) : Reply::reject;
  log::trace("permission", {{"permission", req.permission}, {"subject", req.subject},
                            {"result", reply == Reply::reject ? "reject" : reply == Reply::always ? "always" : "once"}});
  if (reply == Reply::always) always_.insert({req.permission, req.subject});
  return reply != Reply::reject;
}

}  // namespace shaman::permission
