#include "shaman/permission/permission.hpp"

#include "shaman/core/log.hpp"
#include "shaman/core/strings.hpp"

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
      {"todo", "allow"}, {"task", "allow"},
      {"edit", "ask"}, {"webfetch", "ask"}, {"external_directory", "ask"}, {"mcp", "ask"},
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

Json Rules::to_json() const {
  Json out = Json::array();
  for (auto& r : rules_)
    out.push_back({{"layer", r.layer}, {"permission", r.permission}, {"pattern", r.pattern},
                   {"action", to_string(r.action)}});
  return out;
}

bool Gate::check(const Request& req) {
  if (yolo_) return true;
  switch (rules_.evaluate(req.permission, req.subject)) {
    case Action::allow: return true;
    case Action::deny:
      log::trace("permission", {{"permission", req.permission}, {"subject", req.subject}, {"result", "deny"}});
      return false;
    case Action::ask: break;
  }
  if (always_.contains({req.permission, req.subject}) || always_.contains({req.permission, "*"})) return true;
  Reply reply = asker_ ? asker_(req) : Reply::reject;
  log::trace("permission", {{"permission", req.permission}, {"subject", req.subject},
                            {"result", reply == Reply::reject ? "reject" : reply == Reply::always ? "always" : "once"}});
  if (reply == Reply::always) always_.insert({req.permission, req.subject});
  return reply != Reply::reject;
}

}  // namespace shaman::permission
