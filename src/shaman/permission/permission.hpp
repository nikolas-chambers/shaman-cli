#pragma once

#include <atomic>
#include <functional>
#include <optional>
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
  // Evaluate a shell command: every simple command must be allowed, and
  // writes/substitutions downgrade "allow" to "ask". Explicit denies anywhere win.
  Action evaluate_shell(std::string_view permission, const std::string& command) const;
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

// Shell command analysis for bash permissions. Rules are matched against each
// simple command in a chain, and constructs that can write or run hidden code
// force at least "ask", so `cat x > file` or `ls && rm -rf dir` can't ride on
// a read-only allow rule.
struct ShellAnalysis {
  std::vector<std::string> commands;  // simple commands split on ; && || | & and newlines
  bool writes = false;                // output redirection to a file (> >> &> >|), tee
  bool substitution = false;          // $( ), backticks, <( ), >( )
};
ShellAnalysis analyze_shell(const std::string& command);

struct Request {
  std::string permission;  // "bash"
  std::string subject;     // "git push origin main"
  std::string title;       // human-readable summary for the prompt
};

enum class Reply { once, always, reject };
using Asker = std::function<Reply(const Request&)>;

// Evaluates rules and consults the user for "ask", remembering "always".
// A hook consulted before rules (plugins); nullopt defers to the rules.
using Hook = std::function<std::optional<Action>(const Request&)>;

// How much to ask. Explicit "deny" rules hold in every mode.
enum class Mode {
  normal,        // follow the rules
  accept_edits,  // file edits inside the project are allowed without asking; everything else as normal
  yolo,          // every "ask" becomes "allow" (--yolo)
};
const char* to_string(Mode m);
std::optional<Mode> parse_mode(std::string_view s);  // "default"/"normal", "acceptEdits"/"accept-edits", "yolo"

class Gate {
 public:
  Gate(Rules rules, Asker asker, Hook hook = nullptr)
      : rules_(std::move(rules)), asker_(std::move(asker)), hook_(std::move(hook)) {}
  bool check(const Request& req);
  // --yolo: every "ask" becomes "allow". Explicit "deny" rules still hold, so
  // a read-only agent stays read-only and `rm -rf /` stays refused.
  void allow_all() { mode_ = Mode::yolo; }
  void set_mode(Mode m) { mode_ = m; }
  // Follow a mode owned elsewhere (the runner's, which the UI changes while a turn runs).
  void follow(const std::atomic<Mode>* mode) { shared_mode_ = mode; }
  Mode mode() const { return shared_mode_ ? shared_mode_->load() : mode_.load(); }

 private:
  Rules rules_;
  Asker asker_;
  Hook hook_;
  std::set<std::pair<std::string, std::string>> always_;
  std::atomic<Mode> mode_ = Mode::normal;
  const std::atomic<Mode>* shared_mode_ = nullptr;
};

}  // namespace shaman::permission
