#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "shaman/core/json.hpp"

namespace shaman::permission {

enum class Action { allow, ask, deny };

std::string_view to_string(Action a);

// Layered permission rules.
//
//   { "edit": "ask",
//     "bash": { "*": "ask", "git status*": "allow", "rm -rf *": "deny" },
//     "webfetch": "allow" }
//
// Keys are permission names ("edit", "bash", "read", ...). A string value
// applies to every subject; an object maps wildcard patterns over the subject
// (a command, a path, a URL) to actions. The newest layer with any matching
// rule decides; within a layer the most specific pattern wins.
class Rules {
 public:
  static Rules defaults();
  static Rules from_json(const Json& j);

  void push(const Rules& layer);  // `layer` takes priority over what is here
  Action evaluate(std::string_view permission, std::string_view subject) const;
  Json to_json() const;

 private:
  struct Rule {
    std::string permission;
    std::string pattern;
    Action action;
    int layer;
  };
  std::vector<Rule> rules_;
  int layers_ = 0;
};

struct Request {
  std::string permission;  // "bash"
  std::string subject;     // "git push origin main"
  std::string title;       // human-readable summary for the prompt
};

enum class Reply { once, always, reject };
using Asker = std::function<Reply(const Request&)>;

// Evaluates rules and consults the user for "ask", remembering "always".
class Gate {
 public:
  Gate(Rules rules, Asker asker) : rules_(std::move(rules)), asker_(std::move(asker)) {}
  bool check(const Request& req);
  void allow_all() { yolo_ = true; }

 private:
  Rules rules_;
  Asker asker_;
  std::set<std::pair<std::string, std::string>> always_;
  bool yolo_ = false;
};

}  // namespace shaman::permission
