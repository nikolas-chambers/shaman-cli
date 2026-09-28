// bash, webfetch, todowrite, todoread, task
#include <format>
#include <thread>
#include <regex>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"
#include "shaman/tool/builtin/common.hpp"

namespace shaman::tool::detail {
namespace {

class Bash final : public Tool {
 public:
  std::string name() const override { return "bash"; }
  std::string description() const override {
    return "Run a shell command in the project root and return its combined output. Default timeout "
           "120000 ms, max 600000. Avoid interactive commands. Use read/grep/glob instead of cat/grep/find.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"command", {{"type", "string"}}},
                            {"description", {{"type", "string"}, {"description", "5-10 word summary"}}},
                            {"timeout", {{"type", "integer"}, {"description", "Milliseconds"}}},
                            {"workdir", {{"type", "string"}}},
                            {"background", {{"type", "boolean"}, {"description", "Run in the background (servers, watchers); "
                                                                                 "read output with bash_output, stop with bash_kill"}}}}},
            {"required", {"command"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto command = in.value("command", "");
    if (command.empty()) return error("command is required");
    auto cwd = ctx.path(in.value("workdir", ctx.root.string()));
    if (!cwd) return error(cwd.error().message);
    // Always show the real command; the model's description alone could hide what runs.
    auto desc = in.value("description", "");
    auto title = desc.empty() || desc == command ? "$ " + command : desc + "  $ " + command;
    if (!ctx.permit("bash", command, "$ " + command)) return error("permission denied: " + command);
    std::string run = command;
    if (ctx.sandbox) {
      auto wrapped = ctx.sandbox(command, *cwd);
      if (!wrapped) return error("sandbox unavailable, command not run: " + wrapped.error().message);
      run = *wrapped;
    }
    if (in.value("background", false)) {
      if (!ctx.jobs) return error("background jobs are not available here");
      auto job = process::Background::start(run, *cwd);
      if (!job) return error(job.error().message);
      auto id = "job" + std::to_string(ctx.jobs->size() + 1);
      (*ctx.jobs)[id] = *job;
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // catch startup errors and first output
      auto first = (*job)->take_output();
      bool alive = (*job)->running();
      return {truncate(std::format("started {} ({}){}\n{}", id, alive ? "running" : "exited " + std::to_string((*job)->exit_code()),
                                   alive ? "; read more with bash_output" : "", first)),
              !alive && (*job)->exit_code() != 0, "$ " + command + " &"};
    }
    int64_t timeout = std::clamp<int64_t>(in.value("timeout", 120'000), 1'000, 600'000);
    auto res = process::shell(run, {.cwd = *cwd, .timeout = std::chrono::milliseconds(timeout), .cancel = ctx.cancel});
    if (!res) return error(res.error().message);
    auto out = truncate(res->output);
    if (res->timed_out) out += std::format("\n[timed out after {} ms]", timeout);
    if (res->cancelled) out += "\n[cancelled]";
    out += std::format("\n[exit code {}]", res->exit_code);
    return {out, res->exit_code != 0, title};
  }
};

std::string html_to_text(std::string html) {
  static const std::regex drop(R"(<(script|style|noscript|svg)[^>]*>[\s\S]*?</\1>)", std::regex::icase);
  static const std::regex block(R"(<(br|/p|/div|/li|/h[1-6]|/tr)[^>]*>)", std::regex::icase);
  static const std::regex tags(R"(<[^>]+>)");
  static const std::regex blank(R"(\n\s*\n\s*\n+)");
  html = std::regex_replace(html, drop, "");
  html = std::regex_replace(html, block, "\n");
  html = std::regex_replace(html, tags, "");
  for (auto [from, to] : {std::pair{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
                          {"&#39;", "'"}, {"&nbsp;", " "}})
    html = str::replace_all(html, from, to);
  return std::regex_replace(html, blank, "\n\n");
}

class WebFetch final : public Tool {
 public:
  std::string name() const override { return "webfetch"; }
  std::string description() const override {
    return "Fetch a URL. format is text (default, HTML stripped) or html (raw).";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"url", {{"type", "string"}}},
                            {"format", {{"type", "string"}, {"enum", {"text", "html"}}}}}},
            {"required", {"url"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto url = in.value("url", "");
    if (!url.starts_with("http://") && !url.starts_with("https://")) return error("url must be http(s)");
    if (!ctx.permit("webfetch", url, "Fetch " + url)) return error("permission denied");
    http::Request req;
    req.url = url;
    req.timeout_s = 30;
    req.cancel = ctx.cancel;
    auto res = http::send(req);
    if (!res) return error(res.error().message);
    if (res->status >= 400) return error(std::format("HTTP {}", res->status));
    auto body = in.value("format", "text") == "html" ? res->body : html_to_text(res->body);
    return {truncate(body), false, "Fetch " + url};
  }
};

class TodoWrite final : public Tool {
 public:
  std::string name() const override { return "todowrite"; }
  std::string description() const override {
    return "Replace the session's todo list. Use it to plan multi-step work and mark progress: "
           "exactly one item in_progress at a time.";
  }
  Json schema() const override {
    Json item = {{"type", "object"},
                 {"properties", {{"id", {{"type", "string"}}}, {"content", {{"type", "string"}}},
                                 {"status", {{"type", "string"}, {"enum", {"pending", "in_progress", "completed", "cancelled"}}}},
                                 {"priority", {{"type", "string"}, {"enum", {"high", "medium", "low"}}}}}},
                 {"required", {"content", "status"}}};
    return {{"type", "object"}, {"properties", {{"todos", {{"type", "array"}, {"items", item}}}}}, {"required", {"todos"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.todos) return error("todos unavailable");
    ctx.todos->clear();
    int n = 0;
    for (auto& t : in.value("todos", Json::array()))
      ctx.todos->push_back({t.value("id", std::to_string(++n)), t.value("content", ""),
                            t.value("status", "pending"), t.value("priority", "medium")});
    return {render(*ctx.todos), false, std::format("Todos ({})", ctx.todos->size())};
  }
  static std::string render(const std::vector<Todo>& todos) {
    std::string out;
    for (auto& t : todos) {
      auto box = t.status == "completed" ? "[x]" : t.status == "in_progress" ? "[~]" : t.status == "cancelled" ? "[-]" : "[ ]";
      out += std::format("{} {}\n", box, t.content);
    }
    return out.empty() ? "(empty)" : out;
  }
};

class TodoRead final : public Tool {
 public:
  std::string name() const override { return "todoread"; }
  std::string description() const override { return "Show the session's todo list."; }
  Json schema() const override { return {{"type", "object"}, {"properties", Json::object()}}; }
  Output run(const Json&, Context& ctx) override {
    return {ctx.todos ? TodoWrite::render(*ctx.todos) : "(empty)", false, "Todos"};
  }
};

class Task final : public Tool {
 public:
  std::string name() const override { return "task"; }
  std::string description() const override {
    return "Delegate a self-contained task to a subagent with its own context window. Use for broad "
           "searches or research whose details you don't need. subagent_type: explore (read-only, "
           "fast) or general. The subagent returns one final report.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"description", {{"type", "string"}}}, {"prompt", {{"type", "string"}}},
                            {"subagent_type", {{"type", "string"}}},
                            {"background", {{"type", "boolean"}, {"description", "Start it and continue working; collect the "
                                                                                 "result later with task_output. Start several to work in parallel."}}}}},
            {"required", {"description", "prompt"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.subagent) return error("subagents unavailable here");
    auto agent = in.value("subagent_type", "explore");
    if (!ctx.permit("task", agent, "Subagent " + agent + ": " + in.value("description", ""))) return error("permission denied");
    if (in.value("background", false)) {
      if (!ctx.subagent_start) return error("background subagents are unavailable here");
      auto id = ctx.subagent_start(agent, in.value("prompt", ""));
      if (!id) return error(id.error().message);
      return {"started background subagent " + *id + "; collect its report with task_output", false,
              agent + " (background): " + in.value("description", "")};
    }
    auto res = ctx.subagent(agent, in.value("prompt", ""));
    if (!res) return error(res.error().message);
    return {truncate(*res), false, agent + ": " + in.value("description", "")};
  }
};

}  // namespace

std::unique_ptr<Tool> make_bash() { return std::make_unique<Bash>(); }
std::unique_ptr<Tool> make_webfetch() { return std::make_unique<WebFetch>(); }
std::unique_ptr<Tool> make_todowrite() { return std::make_unique<TodoWrite>(); }
std::unique_ptr<Tool> make_todoread() { return std::make_unique<TodoRead>(); }
std::unique_ptr<Tool> make_task() { return std::make_unique<Task>(); }

namespace {
class TaskOutput final : public Tool {
 public:
  std::string name() const override { return "task_output"; }
  std::string description() const override {
    return "Get the report of a background subagent started with task background=true. wait=true (default) blocks "
           "until it finishes; wait=false returns immediately if it is still running.";
  }
  Json schema() const override {
    return {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}, {"wait", {{"type", "boolean"}}}}}, {"required", {"id"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.subagent_result) return error("no background subagents");
    auto r = ctx.subagent_result(in.value("id", ""), in.value("wait", true));
    if (!r) return error(r.error().message);
    return {truncate(*r), false, "Report from " + in.value("id", "")};
  }
};
}  // namespace

std::unique_ptr<Tool> make_task_output() { return std::make_unique<TaskOutput>(); }

}  // namespace shaman::tool::detail

namespace shaman::tool {

void register_builtins(Registry& r, const fs::path& root) {
  using namespace detail;
  for (auto make : {make_read, make_write, make_edit, make_apply_patch, make_list, make_glob, make_grep, make_bash,
                    make_webfetch, make_websearch, make_todowrite, make_todoread, make_task})
    r.add(make());
  r.add(make_skill(root));
  r.add(make_symbols(root));
  for (auto make : {make_lsp, make_multiedit, make_question, make_batch, make_http, make_notebook_edit, make_bash_output,
                    make_bash_kill, make_bash_input, make_memory, make_task_output, make_plan_exit})
    r.add(make());
}

}  // namespace shaman::tool
