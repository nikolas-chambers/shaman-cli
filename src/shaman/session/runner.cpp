#include "shaman/session/runner.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "shaman/agent/prompts.hpp"
#include "shaman/core/id.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/session/input.hpp"
#include "shaman/session/snapshot.hpp"
#include "shaman/session/system_prompt.hpp"

namespace shaman::session {

using namespace llm;

namespace {

// Subagent runs print nothing themselves; their tool calls surface as notices.
class QuietEvents final : public Events {
 public:
  QuietEvents(Events& parent, std::string agent) : parent_(parent), agent_(std::move(agent)) {}
  void tool_end(const ToolCallPart&, const tool::Output& out) override {
    parent_.notice(std::format("  {} > {}", agent_, out.title));
  }
  void notice(std::string_view n) override { parent_.notice(n); }

 private:
  Events& parent_;
  std::string agent_;
};

Snapshot snapshot_for(const std::filesystem::path& root) {
  return Snapshot(root, paths::data_dir() / "snapshots" / paths::project_id(root));
}

}  // namespace

Runner::Runner(Services services) : s_(std::move(services)) {}

permission::Gate& Runner::gate_for(const agent::Agent& agent) {
  auto& g = gates_[agent.name];
  if (!g) {
    auto rules = permission::Rules::defaults();
    rules.push(permission::Rules::from_json(s_.config->permission));
    rules.push(permission::Rules::from_json(agent.permission));
    permission::Hook hook;
    if (s_.plugins && !s_.plugins->empty())
      hook = [plugins = s_.plugins](const permission::Request& r) { return plugins->permission_ask(r); };
    g = std::make_unique<permission::Gate>(std::move(rules), s_.asker, std::move(hook));
    if (s_.allow_all) g->allow_all();
  }
  return *g;
}

Result<provider::Resolved> Runner::pick_model(const Info& session, const agent::Agent& agent,
                                              const std::optional<std::string>& override_ref) const {
  if (override_ref) return s_.providers->resolve(*override_ref);
  if (agent.model) return s_.providers->resolve(*agent.model);
  if (!session.model.empty()) return s_.providers->resolve(session.model);
  return s_.providers->default_model();
}

Result<Runner::Turn> Runner::complete(provider::Resolved& model, const ChatRequest& base, Events& events) {
  // Retry transient failures with backoff, then walk the free fallback chain.
  std::vector<std::string> chain{model.ref()};
  if (s_.config->free_fallback && model.model.free())
    for (auto& f : s_.providers->free_fallbacks(model.ref())) chain.push_back(f);

  Error last;
  for (size_t m = 0; m < chain.size(); ++m) {
    if (m > 0) {
      auto next = s_.providers->resolve(chain[m]);
      if (!next) continue;
      events.notice(std::format("{} unavailable ({}); switching to {}", model.ref(), last.message, next->ref()));
      log::debug(log::Cat::provider, "fallback {} -> {}", model.ref(), next->ref());
      model = *next;
    }
    auto provider = model.connect();
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (s_.cancel && s_.cancel->load()) return fail("cancelled");
      if (attempt > 0) {
        auto wait = std::chrono::seconds(1 << attempt);
        events.notice(std::format("retrying in {}s: {}", wait.count(), last.message));
        std::this_thread::sleep_for(wait);
      }
      ChatRequest req = base;
      req.model = model.model.id;
      Turn turn;
      std::string text, reasoning;
      auto flush = [&] {
        if (!reasoning.empty()) turn.message.parts.push_back(ReasoningPart{std::move(reasoning)}), reasoning.clear();
        if (!text.empty()) turn.message.parts.push_back(TextPart{std::move(text)}), text.clear();
      };
      bool streamed = false;
      auto r = provider->stream(req, [&](const StreamEvent& ev) {
        streamed = true;
        if (auto* t = std::get_if<TextDelta>(&ev)) text += t->text, events.text(t->text);
        else if (auto* rd = std::get_if<ReasoningDelta>(&ev)) reasoning += rd->text, events.reasoning(rd->text);
        else if (auto* tc = std::get_if<ToolCallEvent>(&ev)) flush(), turn.message.parts.push_back(tc->call);
        else if (auto* u = std::get_if<UsageEvent>(&ev)) turn.usage = u->usage;
        else if (auto* f = std::get_if<FinishEvent>(&ev)) turn.finish = f->reason;
      });
      flush();
      if (r) return turn;
      last = r.error();
      log::debug(log::Cat::provider, "{} attempt {} failed: {}", model.ref(), attempt + 1, last.message);
      // Never retry once output reached the user; that would duplicate it.
      if (!last.retryable || streamed) return std::unexpected(last);
    }
  }
  return std::unexpected(last);
}

tool::Output Runner::run_tool(const ToolCallPart& call, const agent::Agent& agent, const Info& session, Events& events) {
  auto* t = s_.tools->find(call.name);
  if (!t || !agent.tool_enabled(call.name)) return tool::error("unknown or disabled tool: " + call.name);
  if (call.input.contains("__invalid_json"))
    return tool::error("tool arguments were not valid JSON: " + call.input["__invalid_json"].get<std::string>());

  auto& gate = gate_for(agent);
  auto sig = call.name + call.input.dump();
  recent_calls_.push_back(sig);
  if (recent_calls_.size() > 3) recent_calls_.erase(recent_calls_.begin());
  if (recent_calls_.size() == 3 && recent_calls_[0] == sig && recent_calls_[1] == sig &&
      !gate.check({"doom_loop", call.name, "Same " + call.name + " call three times in a row. Continue?"}))
    return tool::error("stopped: identical tool call repeated three times; try a different approach");

  // Plugins may rewrite or refuse the call first.
  Json input = call.input;
  if (s_.plugins && !s_.plugins->empty()) {
    plugin::ToolBefore before{session.id, call.name, input};
    s_.plugins->tool_before(before);
    if (before.block) return tool::error("blocked by plugin: " + *before.block);
    input = before.input;
  }

  tool::Context ctx;
  ctx.root = s_.root;
  ctx.session_id = session.id;
  ctx.gate = &gate;
  ctx.cancel = s_.cancel;
  ctx.read_files = &read_files_;
  ctx.todos = &todos_;
  ctx.subagent = [&, parent = session](const std::string& agent_name, const std::string& prompt) -> Result<std::string> {
    auto* sub = s_.agents->find(agent_name);
    if (!sub || sub->mode == agent::Mode::primary) return fail("no such subagent: " + agent_name);
    auto child = s_.store->create(sub->name, parent.model, parent.id);
    if (!child) return std::unexpected(child.error());
    QuietEvents quiet(events, sub->name);
    return prompt_as_subagent(*child, prompt, quiet);
  };
  if (s_.lsp) {
    ctx.diagnostics = [lsp = s_.lsp](const std::filesystem::path& p) { return lsp->report(p); };
    ctx.lsp = [lsp = s_.lsp](const std::string& op, const std::filesystem::path& p, int line, int col) {
      return lsp->query(op, p, line, col);
    };
  }

  auto t0 = std::chrono::steady_clock::now();
  log::trace("tool_call", {{"id", call.id}, {"name", call.name}, {"input", call.input}});
  auto out = t->run(input, ctx);
  if (s_.plugins && !s_.plugins->empty()) {
    plugin::ToolAfter after{session.id, call.name, input, out.text, out.is_error};
    s_.plugins->tool_after(after);
    out.text = after.output;
    s_.plugins->event("tool.end", {{"session", session.id}, {"tool", call.name}, {"title", out.title}, {"is_error", out.is_error}});
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  if (log::enabled(log::Cat::tool)) {
    auto preview = out.text.substr(0, 160);
    std::ranges::replace(preview, '\n', ' ');
    log::debug(log::Cat::tool, "{} {} -> {}{} ({}ms)", call.name, call.input.dump().substr(0, 200),
               out.is_error ? "error: " : "", preview, ms);
  }
  log::trace("tool_result", {{"id", call.id}, {"is_error", out.is_error}, {"ms", ms}, {"output", out.text}});
  if (out.title.empty()) out.title = call.name;
  return out;
}

Result<std::string> Runner::prompt(Info& session, const std::string& input_text, Events& events, const PromptOptions& opts) {
  auto* agent = s_.agents->find(session.agent);
  if (!agent) return fail("unknown agent: " + session.agent);
  auto model = pick_model(session, *agent, opts.model);
  if (!model) return std::unexpected(model.error());

  auto history = s_.store->messages(session.id);
  if (!history) return std::unexpected(history.error());
  auto messages = std::move(*history);

  if (s_.config->snapshot && session.parent_id.empty())
    if (auto tree = snapshot_for(s_.root).track()) session.snapshots.push_back(*tree);
  std::string text = input_text;
  if (s_.plugins && !s_.plugins->empty()) s_.plugins->chat_message(session.id, text);
  if (session.title.empty()) session.title = text.substr(0, 80);

  auto composed = compose(text, opts.attachments, s_.root);
  for (auto& w : composed.warnings) events.notice(w);
  for (auto& f : composed.files) read_files_.insert(f);
  auto user = std::move(composed.message);
  if (user.has_images() && !model->model.vision && model->model.free())
    events.notice(model->ref() + " may not support images");
  messages.push_back(user);
  s_.store->append(session.id, user);
  s_.store->save(session);

  ChatRequest req;
  req.system = system_prompt(*agent, *s_.config, s_.root, model->ref());
  if (s_.plugins && !s_.plugins->empty()) {
    s_.plugins->chat_system(agent->name, req.system);
    s_.plugins->event("prompt", {{"session", session.id}, {"agent", agent->name}, {"model", model->ref()}, {"text", text}});
  }
  req.temperature = agent->temperature;
  req.cancel = s_.cancel;
  for (auto* t : s_.tools->all())
    if (agent->tool_enabled(t->name())) req.tools.push_back(t->spec());

  std::string final_text;
  for (int step = 1; step <= agent->max_steps; ++step) {
    log::debug(log::Cat::session, "{} step {} ({} messages, model {})", session.id, step, messages.size(), model->ref());
    req.messages = messages;
    auto turn = complete(*model, req, events);
    if (!turn) {
      s_.store->save(session);
      if (s_.plugins) s_.plugins->event("error", {{"session", session.id}, {"message", turn.error().message}});
      return std::unexpected(turn.error());
    }
    messages.push_back(turn->message);
    s_.store->append(session.id, turn->message);
    session.model = model->ref();
    session.usage.input += turn->usage.input;
    session.usage.output += turn->usage.output;
    session.usage.cache_read += turn->usage.cache_read;
    session.cost += (turn->usage.input * model->model.cost_input + turn->usage.output * model->model.cost_output) / 1e6;
    session.updated = now_ms();
    events.step_end(step, turn->usage, model->ref());
    final_text = turn->message.text();

    auto calls = turn->message.tool_calls();
    if (calls.empty()) break;

    Message results{Role::user, {}};
    for (auto& call : calls) {
      if (s_.cancel && s_.cancel->load()) {
        results.parts.push_back(ToolResultPart{call.id, call.name, "cancelled by user", true});
        continue;
      }
      events.tool_start(call);
      auto out = run_tool(call, *agent, session, events);
      events.tool_end(call, out);
      results.parts.push_back(ToolResultPart{call.id, call.name, out.text, out.is_error});
    }
    messages.push_back(results);
    s_.store->append(session.id, results);
    if (s_.cancel && s_.cancel->load()) break;

    // Compact before the next step if the window is nearly full.
    if (turn->usage.total() > model->model.context * 85 / 100) {
      s_.store->save(session);
      if (auto r = compact(session, events); !r) events.notice("compaction failed: " + r.error().message);
      auto reloaded = s_.store->messages(session.id);
      if (reloaded) messages = std::move(*reloaded);
    }
    if (step == agent->max_steps) events.notice(std::format("stopped after {} steps (agent max_steps)", step));
  }
  s_.store->save(session);
  if (s_.plugins)
    s_.plugins->event("idle", {{"session", session.id}, {"text", final_text}, {"cost", session.cost},
                               {"input_tokens", session.usage.input}, {"output_tokens", session.usage.output}});
  return final_text;
}

Result<std::string> Runner::prompt_as_subagent(Info& child, const std::string& text, Events& events) {
  return prompt(child, text, events);
}

Result<void> Runner::compact(Info& session, Events& events) {
  auto messages = s_.store->messages(session.id);
  if (!messages) return std::unexpected(messages.error());
  if (messages->size() < 2) return {};
  auto model = s_.providers->resolve(session.model);
  if (!model) model = s_.providers->default_model();
  if (!model) return std::unexpected(model.error());

  events.notice("compacting conversation...");
  ChatRequest req;
  req.system = std::string(agent::prompts::compaction);
  req.messages = *messages;
  req.messages.push_back(Message::user("Summarise the conversation above as instructed."));
  req.cancel = s_.cancel;
  class Sink final : public Events {} sink;
  auto turn = complete(*model, req, sink);
  if (!turn) return std::unexpected(turn.error());
  auto summary = turn->message.text();
  if (summary.empty()) return fail("empty summary");
  log::debug(log::Cat::session, "compacted {} messages into {} chars", messages->size(), summary.size());
  return s_.store->replace_messages(session.id, {Message::user("Summary of the conversation so far:\n\n" + summary)});
}

Result<std::string> Runner::undo(Info& session) {
  if (session.snapshots.empty()) return fail("nothing to undo");
  auto tree = session.snapshots.back();
  auto snap = snapshot_for(s_.root);
  auto changed = snap.diff(tree);
  if (auto r = snap.restore(tree); !r) return std::unexpected(r.error());
  session.snapshots.pop_back();
  s_.store->save(session);
  return changed.empty() ? std::string("no file changes to undo") : changed;
}

}  // namespace shaman::session
