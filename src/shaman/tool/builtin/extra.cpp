// multiedit, question, batch, http, notebook_edit, bash_output, bash_kill, memory
#include <format>
#include <future>
#include <mutex>
#include <thread>

#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/diff/diff.hpp"
#include "shaman/http/http.hpp"
#include "shaman/index/index.hpp"
#include "shaman/tool/builtin/common.hpp"
#include "shaman/tool/builtin/edit.hpp"

namespace shaman::tool::detail {

std::string image_type(const fs::path& p) {
  auto ext = str::lower(p.extension().string());
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".webp") return "image/webp";
  return "";
}

static std::string cell_source(const Json& src) {
  if (src.is_string()) return src.get<std::string>();
  std::string out;
  for (auto& line : src) out += line.get<std::string>();
  return out;
}

std::string render_notebook(const std::string& text) {
  Json nb;
  try {
    nb = Json::parse(text);
  } catch (...) {
    return "invalid notebook JSON";
  }
  std::string out;
  int i = 0;
  for (auto& cell : nb.value("cells", Json::array())) {
    out += std::format("--- cell {} [{}]{} ---\n{}\n", i++, cell.value("cell_type", "?"),
                       cell.contains("id") ? " id=" + cell["id"].get<std::string>() : "", cell_source(cell.value("source", Json(""))));
    for (auto& o : cell.value("outputs", Json::array())) {
      std::string t;
      if (o.contains("text")) t = cell_source(o["text"]);
      else if (o.contains("data") && o["data"].contains("text/plain")) t = cell_source(o["data"]["text/plain"]);
      else if (o.contains("ename")) t = o.value("ename", "") + ": " + o.value("evalue", "");
      else if (o.contains("data") && o["data"].contains("image/png")) t = "[image output]";
      if (!t.empty()) out += "[output]\n" + (t.size() > 2000 ? t.substr(0, 2000) + "\n..." : t) + "\n";
    }
  }
  return out.empty() ? "(empty notebook)" : out;
}

namespace {

class MultiEdit final : public Tool {
 public:
  std::string name() const override { return "multiedit"; }
  std::string description() const override {
    return "Make several edits to one file in a single atomic step. Edits apply in order, each to the result of the "
           "previous; if any edit fails, nothing is written. Same matching rules as edit. Read the file first.";
  }
  Json schema() const override {
    Json edit = {{"type", "object"},
                 {"properties", {{"oldString", {{"type", "string"}}}, {"newString", {{"type", "string"}}}, {"replaceAll", {{"type", "boolean"}}}}},
                 {"required", {"oldString", "newString"}}};
    return {{"type", "object"},
            {"properties", {{"filePath", {{"type", "string"}}}, {"edits", {{"type", "array"}, {"items", edit}}}}},
            {"required", {"filePath", "edits"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto p = ctx.path(in.value("filePath", ""));
    if (!p) return error(p.error().message);
    if (ctx.read_files && !ctx.read_files->contains(*p)) return error("read the file before editing it");
    auto content = read_all(*p);
    if (!content) return error("file not found: " + p->string());
    auto edits = in.value("edits", Json::array());
    if (edits.empty()) return error("no edits given");
    std::string text = *content;
    for (size_t i = 0; i < edits.size(); ++i) {
      auto r = apply_edit(text, edits[i].value("oldString", ""), edits[i].value("newString", ""), edits[i].value("replaceAll", false));
      if (!r) return error(std::format("edit {} failed: {} (nothing was written)", i + 1, r.error().message));
      text = *r;
    }
    if (!ctx.permit("edit", p->string(), std::format("Edit {} ({} changes)", rel(ctx, *p), edits.size()))) return error("permission denied");
    if (!write_all(*p, text)) return error("failed to write " + p->string());
    auto diag = ctx.diagnostics ? ctx.diagnostics(*p) : "";
    Output out{std::format("Applied {} edits to {}{}", edits.size(), rel(ctx, *p), diag), false, ""};
    out.diff = diff::unified(*content, read_all(*p).value_or(text), rel(ctx, *p));
    auto st = diff::stats(out.diff);
    out.title = std::format("Edit {} (+{} -{})", rel(ctx, *p), st.added, st.removed);
    return out;
  }
};

class Question final : public Tool {
 public:
  std::string name() const override { return "question"; }
  std::string description() const override {
    return "Ask the user a question when a decision is genuinely theirs and a wrong guess would be costly. Offer 2-5 "
           "short options when possible (the user can also type their own answer). Don't ask about things you can "
           "find out or decide sensibly yourself.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"question", {{"type", "string"}}},
                            {"options", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                            {"multiple", {{"type", "boolean"}, {"description", "Allow several options"}}}}},
            {"required", {"question"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.question) return error("no user is available to answer; make a sensible choice, state it, and continue");
    tool::Question q{in.value("question", ""), in.value("options", std::vector<std::string>{}), in.value("multiple", false)};
    auto r = ctx.question(q);
    if (!r) return error(r.error().message);
    return {"User answered: " + *r, false, "Asked: " + q.question.substr(0, 60)};
  }
};

class PlanExit final : public Tool {
 public:
  std::string name() const override { return "plan_exit"; }
  std::string description() const override {
    return "Present your finished plan to the user for approval. Call it once the plan is concrete (files to change, "
           "steps, how you will verify). If approved, you switch to the build agent and should start implementing "
           "straight away; if not, you get the user's feedback to revise the plan.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"plan", {{"type", "string"}, {"description", "The plan, in Markdown"}}}}},
            {"required", {"plan"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto plan = in.value("plan", "");
    if (plan.empty()) return error("give the plan");
    if (!ctx.question || !ctx.switch_agent)
      return error("nobody can approve a plan here; give the plan as your final answer instead");
    static const std::string yes = "Yes, build it", yes_edits = "Yes, and auto-accept edits", no = "No, keep planning";
    auto r = ctx.question({plan + "\n\nApprove this plan?", {yes, yes_edits, no}, false});
    if (!r) return error(r.error().message);
    if (*r == yes || *r == yes_edits) {
      ctx.switch_agent("build", *r == yes_edits ? "acceptEdits" : "");
      return {"The user approved the plan. You are now the build agent with edit tools: implement the plan, then verify it.",
              false, "Plan approved"};
    }
    auto feedback = *r == no || r->empty() ? std::string("no specific feedback") : *r;
    return {"The user did not approve the plan (" + feedback + "). Revise it, or ask what to change.", false, "Plan not approved"};
  }
};

// Jump to code: where things are defined, a file's outline, or a map of the repo (the codebase index).
class Symbols final : public Tool {
 public:
  explicit Symbols(fs::path root) : root_(std::move(root)) {}
  std::string name() const override { return "symbols"; }
  std::string description() const override {
    return "Find code by name using the project's symbol index (functions, classes, types across 25 languages). "
           "operation 'find' with query: where a symbol is defined (exact, then partial matches). 'outline' with "
           "filePath: the definitions in a file with line numbers. 'map' (optional path): the repository layout "
           "with each file's main symbols; start here in an unfamiliar or large codebase. Use grep for usages.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"operation", {{"type", "string"}, {"enum", {"find", "outline", "map"}}}},
                            {"query", {{"type", "string"}}},
                            {"filePath", {{"type", "string"}}},
                            {"path", {{"type", "string"}, {"description", "map: only this directory"}}}}},
            {"required", {"operation"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    std::lock_guard lock(mu_);
    if (!index_)
      index_ = std::make_unique<index::Index>(root_, paths::data_dir() / "projects" / paths::project_id(root_) / "index.json");
    index_->refresh();
    auto op = in.value("operation", "");
    auto line = [&](const index::Symbol& s) { return std::format("{}:{}  {} {}", s.file, s.line, s.kind, s.name); };
    if (op == "find") {
      auto q = in.value("query", "");
      if (q.empty()) return error("find needs a query");
      auto hits = index_->find(q);
      if (hits.empty()) return {"no definitions matching '" + q + "' (try grep for text)", false, "Symbols: " + q};
      std::string out;
      for (auto& s : hits) out += line(s) + "\n";
      return {out, false, std::format("Find {} ({} found)", q, hits.size())};
    }
    if (op == "outline") {
      auto p = ctx.path(in.value("filePath", ""));
      if (!p) return error(p.error().message);
      auto rel = fs::relative(*p, root_).generic_string();
      auto syms = index_->outline(rel);
      if (syms.empty()) return {"no definitions found in " + rel, false, "Outline " + rel};
      std::string out;
      for (auto& s : syms) out += std::format("{:>5}  {} {}\n", s.line, s.kind, s.name);
      return {out, false, std::format("Outline {} ({} symbols)", rel, syms.size())};
    }
    if (op == "map") {
      auto out = index_->map(in.value("path", ""));
      return {out.empty() ? "no indexed source files" : out, false,
              std::format("Repo map ({} files, {} symbols)", index_->file_count(), index_->symbol_count())};
    }
    return error("operation must be find, outline or map");
  }

 private:
  fs::path root_;
  std::mutex mu_;
  std::unique_ptr<index::Index> index_;
};

class Batch final : public Tool {
 public:
  std::string name() const override { return "batch"; }
  std::string description() const override {
    return "Run several read-only tool calls in parallel and get all results at once: read, glob, grep, list, lsp, "
           "webfetch, websearch. Use it to gather context quickly (e.g. read five files, or grep three patterns).";
  }
  Json schema() const override {
    Json call = {{"type", "object"}, {"properties", {{"tool", {{"type", "string"}}}, {"input", {{"type", "object"}}}}}, {"required", {"tool", "input"}}};
    return {{"type", "object"}, {"properties", {{"calls", {{"type", "array"}, {"items", call}, {"maxItems", 20}}}}}, {"required", {"calls"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    static const std::set<std::string> allowed{"read", "glob", "grep", "list", "lsp", "webfetch", "websearch"};
    if (!ctx.tools) return error("batch unavailable here");
    auto calls = in.value("calls", Json::array());
    if (calls.empty() || calls.size() > 20) return error("give 1-20 calls");
    std::vector<std::future<Output>> futures;
    for (auto& c : calls) {
      auto name = c.value("tool", "");
      auto* t = ctx.tools->find(name);
      if (!t || !allowed.contains(name)) {
        std::promise<Output> p;
        p.set_value(error("not allowed in batch: " + name));
        futures.push_back(p.get_future());
        continue;
      }
      futures.push_back(std::async(std::launch::async, [t, input = c.value("input", Json::object()), &ctx] {
        Context local = ctx;  // shares the gate and read-set pointers
        return t->run(input, local);
      }));
    }
    std::string out;
    int failed = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
      auto o = futures[i].get();
      failed += o.is_error;
      out += std::format("=== {} {} {}===\n{}\n\n", i + 1, calls[i].value("tool", ""), o.is_error ? "(error) " : "", o.text);
    }
    return {truncate(out), false, std::format("Batch ({} calls{})", calls.size(), failed ? std::format(", {} failed", failed) : "")};
  }
};

class Http final : public Tool {
 public:
  std::string name() const override { return "http"; }
  std::string description() const override {
    return "Make an HTTP request (any method, headers, body) and see the status, response headers and body. Use for "
           "testing APIs and local servers; use webfetch to read web pages.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"method", {{"type", "string"}, {"description", "GET (default), POST, PUT, PATCH, DELETE, HEAD"}}},
                            {"url", {{"type", "string"}}},
                            {"headers", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}},
                            {"body", {{"type", "string"}}},
                            {"timeout", {{"type", "integer"}, {"description", "Seconds, default 30"}}}}},
            {"required", {"url"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto url = in.value("url", "");
    auto method = str::lower(in.value("method", "GET"));
    for (auto& ch : method) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    if (!url.starts_with("http://") && !url.starts_with("https://")) return error("url must be http(s)");
    if (!ctx.permit("http", method + " " + url, method + " " + url)) return error("permission denied");
    http::Request req;
    req.method = method;
    req.url = url;
    req.body = in.value("body", "");
    req.timeout_s = std::clamp<long>(in.value("timeout", 30), 1, 300);
    req.cancel = ctx.cancel;
    for (auto& [k, v] : in.value("headers", Json::object()).items()) req.headers.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump());
    auto res = http::send(req);
    if (!res) return error(res.error().message);
    std::string out = std::format("HTTP {}\n", res->status);
    for (auto& [k, v] : res->headers) out += k + ": " + v + "\n";
    auto body = res->body;
    if (res->header("content-type").find("json") != std::string::npos) {
      try {
        body = Json::parse(body).dump(2);
      } catch (...) {
      }
    }
    out += "\n" + body;
    return {truncate(out), res->status >= 400, std::format("{} {} -> {}", method, url, res->status)};
  }
};

class NotebookEdit final : public Tool {
 public:
  std::string name() const override { return "notebook_edit"; }
  std::string description() const override {
    return "Edit a Jupyter notebook (.ipynb) cell: replace a cell's source, insert a new cell after an index, or "
           "delete a cell. Read the notebook first to see cell indices. Outputs of edited cells are cleared.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"notebookPath", {{"type", "string"}}},
                            {"cellIndex", {{"type", "integer"}, {"description", "0-based; for insert, the new cell goes after it (-1 = at the start)"}}},
                            {"newSource", {{"type", "string"}}},
                            {"cellType", {{"type", "string"}, {"enum", {"code", "markdown"}}}},
                            {"editMode", {{"type", "string"}, {"enum", {"replace", "insert", "delete"}}}}}},
            {"required", {"notebookPath", "cellIndex"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto p = ctx.path(in.value("notebookPath", ""));
    if (!p) return error(p.error().message);
    if (ctx.read_files && !ctx.read_files->contains(*p)) return error("read the notebook before editing it");
    auto content = read_all(*p);
    if (!content) return error("notebook not found");
    Json nb;
    try {
      nb = Json::parse(*content);
    } catch (...) {
      return error("invalid notebook JSON");
    }
    auto& cells = nb["cells"];
    int idx = in.value("cellIndex", 0);
    auto mode = in.value("editMode", "replace");
    auto lines = [](const std::string& s) {
      Json arr = Json::array();
      size_t start = 0;
      for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\n') arr.push_back(s.substr(start, i - start + 1)), start = i + 1;
      if (start < s.size()) arr.push_back(s.substr(start));
      return arr;
    };
    if (mode == "insert") {
      if (idx < -1 || idx >= int(cells.size())) return error("cellIndex out of range");
      auto type = in.value("cellType", "code");
      Json cell = {{"cell_type", type}, {"metadata", Json::object()}, {"source", lines(in.value("newSource", ""))}};
      if (type == "code") cell["outputs"] = Json::array(), cell["execution_count"] = nullptr;
      cells.insert(cells.begin() + (idx + 1), cell);
    } else {
      if (idx < 0 || idx >= int(cells.size())) return error("cellIndex out of range");
      if (mode == "delete") cells.erase(cells.begin() + idx);
      else {
        cells[idx]["source"] = lines(in.value("newSource", ""));
        if (in.contains("cellType")) cells[idx]["cell_type"] = in["cellType"];
        if (cells[idx]["cell_type"] == "code") cells[idx]["outputs"] = Json::array(), cells[idx]["execution_count"] = nullptr;
      }
    }
    if (!ctx.permit("edit", p->string(), std::format("Notebook {} ({} cell {})", rel(ctx, *p), mode, idx))) return error("permission denied");
    if (!write_all(*p, nb.dump(1) + "\n")) return error("failed to write notebook");
    return {std::format("{} cell {} in {} ({} cells)", mode, idx, rel(ctx, *p), cells.size()), false,
            std::format("Notebook {} {}", mode, rel(ctx, *p))};
  }
};

class BashOutput final : public Tool {
 public:
  std::string name() const override { return "bash_output"; }
  std::string description() const override {
    return "Get new output from a background job started with bash background=true, and whether it is still running. "
           "Optional filter keeps only lines matching a substring.";
  }
  Json schema() const override {
    return {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}, {"filter", {{"type", "string"}}}}}, {"required", {"id"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.jobs) return error("no background jobs");
    auto it = ctx.jobs->find(in.value("id", ""));
    if (it == ctx.jobs->end()) {
      std::string ids;
      for (auto& [id, job] : *ctx.jobs) ids += " " + id;
      return error("no such job; running jobs:" + (ids.empty() ? " none" : ids));
    }
    auto out = it->second->take_output();
    if (auto f = in.value("filter", ""); !f.empty()) {
      std::string kept;
      for (auto& l : str::lines(out))
        if (l.find(f) != std::string::npos) kept += l + "\n";
      out = kept;
    }
    bool running = it->second->running();
    auto status = running ? "running" : "exited with code " + std::to_string(it->second->exit_code());
    return {truncate(std::format("[{} {}]\n{}", it->first, status, out.empty() ? "(no new output)" : out)), false,
            std::format("{} ({})", it->first, running ? "running" : "exited")};
  }
};

class BashKill final : public Tool {
 public:
  std::string name() const override { return "bash_kill"; }
  std::string description() const override { return "Stop a background job started with bash background=true."; }
  Json schema() const override { return {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}}; }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.jobs) return error("no background jobs");
    auto it = ctx.jobs->find(in.value("id", ""));
    if (it == ctx.jobs->end()) return error("no such job");
    auto tail = it->second->take_output();
    it->second->kill();
    ctx.jobs->erase(it);
    return {"stopped " + in.value("id", "") + (tail.empty() ? "" : "\nlast output:\n" + truncate(tail, 50)), false,
            "Stopped " + in.value("id", "")};
  }
};

// Durable project notes: .shaman/MEMORY.md, included in the system prompt of every session.
class Memory final : public Tool {
 public:
  std::string name() const override { return "memory"; }
  std::string description() const override {
    return "Project memory that persists across sessions (.shaman/MEMORY.md, loaded into every session). Save facts "
           "worth remembering: build/test commands, conventions, decisions, gotchas, user preferences. action: view, "
           "add (append one note), or replace (rewrite the whole file, to reorganise or remove stale notes). Keep notes "
           "short and factual; never store secrets.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"action", {{"type", "string"}, {"enum", {"view", "add", "replace"}}}}, {"text", {{"type", "string"}}}}},
            {"required", {"action"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto path = ctx.root / ".shaman" / "MEMORY.md";
    auto action = in.value("action", "view");
    auto current = read_all(path).value_or("");
    if (action == "view") return {current.empty() ? "(memory is empty)" : current, false, "Memory"};
    auto text = str::trim(in.value("text", ""));
    if (text.empty()) return error("text is required");
    if (!ctx.permit("memory", action, "Update project memory (.shaman/MEMORY.md)")) return error("permission denied");
    std::string updated = action == "replace" ? text + "\n"
                                              : (current.empty() ? "# Project memory\n\n" : current + (current.ends_with("\n") ? "" : "\n")) +
                                                    "- " + text + "\n";
    if (updated.size() > 20'000) return error("memory would exceed 20 KB; replace it with a condensed version");
    if (!write_all(path, updated)) return error("cannot write " + path.string());
    return {action == "add" ? "Saved to memory." : "Memory rewritten.", false, "Memory " + action};
  }
};

}  // namespace

std::unique_ptr<Tool> make_multiedit() { return std::make_unique<MultiEdit>(); }
std::unique_ptr<Tool> make_plan_exit() { return std::make_unique<PlanExit>(); }
std::unique_ptr<Tool> make_symbols(const fs::path& root) { return std::make_unique<Symbols>(root); }
std::unique_ptr<Tool> make_question() { return std::make_unique<Question>(); }
std::unique_ptr<Tool> make_batch() { return std::make_unique<Batch>(); }
std::unique_ptr<Tool> make_http() { return std::make_unique<Http>(); }
std::unique_ptr<Tool> make_notebook_edit() { return std::make_unique<NotebookEdit>(); }
std::unique_ptr<Tool> make_bash_output() { return std::make_unique<BashOutput>(); }
std::unique_ptr<Tool> make_bash_kill() { return std::make_unique<BashKill>(); }
std::unique_ptr<Tool> make_memory() { return std::make_unique<Memory>(); }

}  // namespace shaman::tool::detail

namespace shaman::tool::detail {
namespace {

class BashInput final : public Tool {
 public:
  std::string name() const override { return "bash_input"; }
  std::string description() const override {
    return "Send input to a background job's stdin (answer a prompt, drive a REPL). A newline is appended unless "
           "enter=false. Then read the reply with bash_output.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"id", {{"type", "string"}}}, {"input", {{"type", "string"}}}, {"enter", {{"type", "boolean"}}}}},
            {"required", {"id", "input"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.jobs) return error("no background jobs");
    auto it = ctx.jobs->find(in.value("id", ""));
    if (it == ctx.jobs->end()) return error("no such job");
    auto text = in.value("input", "") + (in.value("enter", true) ? "\n" : "");
    if (!ctx.permit("bash", "input to " + it->second->command(), "Send input to " + it->first)) return error("permission denied");
    if (auto r = it->second->write_input(text); !r) return error(r.error().message);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto out = it->second->take_output();
    return {truncate("sent.\n" + (out.empty() ? "(no output yet; use bash_output)" : out)), false, "Input to " + it->first};
  }
};

}  // namespace

std::unique_ptr<Tool> make_bash_input() { return std::make_unique<BashInput>(); }

}  // namespace shaman::tool::detail
