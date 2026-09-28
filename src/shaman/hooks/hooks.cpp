#include "shaman/hooks/hooks.hpp"

#include <regex>

#include "shaman/core/log.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::hooks {

namespace {
constexpr Event kAll[] = {Event::pre_tool_use, Event::post_tool_use, Event::user_prompt_submit,
                          Event::stop, Event::session_start, Event::notification};

bool matches(const std::string& matcher, const std::string& subject) {
  if (matcher.empty() || matcher == "*") return true;
  try {
    return std::regex_match(subject, std::regex(matcher, std::regex::icase));
  } catch (const std::regex_error&) {
    return matcher == subject;
  }
}
}  // namespace

const char* name(Event e) {
  switch (e) {
    case Event::pre_tool_use: return "PreToolUse";
    case Event::post_tool_use: return "PostToolUse";
    case Event::user_prompt_submit: return "UserPromptSubmit";
    case Event::stop: return "Stop";
    case Event::session_start: return "SessionStart";
    case Event::notification: return "Notification";
  }
  return "";
}

Hooks::Hooks(const Json& config, std::filesystem::path root) : root_(std::move(root)) {
  if (!config.is_object()) return;
  for (Event e : kAll) {
    auto it = config.find(name(e));
    if (it == config.end() || !it->is_array()) continue;
    const Json& list = *it;
    for (const Json& item : list) {
      if (!item.is_object()) continue;
      std::string matcher = item.value("matcher", "");
      int timeout = item.value("timeout", 60);
      if (item.contains("command")) entries_.push_back({e, matcher, item.value("command", ""), timeout});
      // also accept the nested form {"matcher", "hooks": [{"type": "command", "command"}]}
      if (auto nested = item.find("hooks"); nested != item.end() && nested->is_array()) {
        const Json& hooks = *nested;
        for (const Json& h : hooks)
          if (h.is_object() && h.contains("command"))
            entries_.push_back({e, matcher, h.value("command", ""), h.value("timeout", timeout)});
      }
    }
  }
  std::erase_if(entries_, [](const Entry& en) { return en.command.empty(); });
  if (!entries_.empty()) log::debug(log::Cat::config, "{} hook(s) configured", entries_.size());
}

bool Hooks::has(Event e) const {
  return std::ranges::any_of(entries_, [e](const Entry& en) { return en.event == e; });
}

Outcome Hooks::run(Event e, const std::string& session_id, const std::string& subject, Json payload) const {
  Outcome outcome;
  payload["hook_event_name"] = name(e);
  payload["session_id"] = session_id;
  payload["cwd"] = root_.string();
  std::string file;
  if (auto in = payload.find("tool_input"); in != payload.end() && in->is_object())
    for (const char* key : {"filePath", "path", "notebookPath"})
      if (in->contains(key) && (*in)[key].is_string()) { file = (*in)[key].get<std::string>(); break; }

  for (const auto& en : entries_) {
    if (en.event != e || !matches(en.matcher, subject)) continue;
    process::Options opts;
    opts.cwd = root_;
    opts.timeout = std::chrono::seconds(en.timeout_s);
    opts.input = payload.dump();
    opts.separate_stderr = true;
    opts.env = {{"SHAMAN_PROJECT_DIR", root_.string()}, {"SHAMAN_SESSION_ID", session_id},
                {"SHAMAN_HOOK_EVENT", name(e)}, {"SHAMAN_TOOL", subject}, {"SHAMAN_FILE", file}};
    auto r = process::shell(en.command, opts);
    log::debug(log::Cat::tool, "hook {} `{}` -> {}", name(e), en.command, r ? std::to_string(r->exit_code) : r.error().message);
    log::trace("hook", {{"event", name(e)}, {"command", en.command}, {"exit", r ? r->exit_code : -1},
                        {"stdout", r ? r->output : ""}, {"stderr", r ? r->error : ""}});
    if (!r) { outcome.errors.push_back(en.command + ": " + r.error().message); continue; }
    if (r->timed_out) { outcome.errors.push_back(en.command + ": timed out"); continue; }
    auto out = str::trim(r->output);
    // structured answer on stdout
    if (!out.empty() && out.front() == '{') {
      try {
        auto j = Json::parse(out);
        if (j.value("decision", "") == "block") {
          outcome.block = true;
          outcome.reason = j.value("reason", "blocked by hook");
          return outcome;
        }
        if (j.contains("additionalContext")) outcome.context += j.value("additionalContext", "") + "\n";
        continue;
      } catch (...) {}
    }
    if (r->exit_code == 2) {
      outcome.block = true;
      outcome.reason = str::trim(r->error).empty() ? "blocked by hook: " + en.command : std::string(str::trim(r->error));
      return outcome;
    }
    if (r->exit_code != 0) {
      outcome.errors.push_back(std::format("{} exited {}: {}", en.command, r->exit_code, str::trim(r->error)));
      continue;
    }
    if ((e == Event::user_prompt_submit || e == Event::session_start) && !out.empty()) outcome.context += std::string(out) + "\n";
  }
  return outcome;
}

}  // namespace shaman::hooks
