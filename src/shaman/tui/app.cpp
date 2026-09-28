#include "shaman/tui/app.hpp"

#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include "shaman/command/command.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/extras/notify.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/session/archive.hpp"
#include "shaman/tui/markdown.hpp"
#include "shaman/tui/terminal.hpp"
#include "shaman/version.hpp"

namespace shaman::tui {
namespace fs = std::filesystem;
namespace {

constexpr const char* R = "\x1b[0m";
constexpr const char* DIM = "\x1b[2m";
constexpr const char* BOLD = "\x1b[1m";
constexpr const char* ACCENT = "\x1b[35m";
constexpr const char* CYAN = "\x1b[36m";
constexpr const char* RED = "\x1b[31m";
constexpr const char* YELLOW = "\x1b[33m";
constexpr const char* INVERT = "\x1b[7m";

struct Block {
  enum Kind { user, assistant, reasoning, tool, notice, error } kind;
  std::string text;
  std::string detail;  // tool output
  bool failed = false;
  std::string diff;    // file changes (shown inline)
};

struct PickItem {
  std::string label, hint;
  std::function<void()> action;
};

class Ui;

// Bridges runner callbacks (worker thread) to the UI thread.
class Bridge final : public session::Events {
 public:
  explicit Bridge(Ui& ui) : ui_(ui) {}
  void text(std::string_view t) override;
  void reasoning(std::string_view t) override;
  void tool_start(const llm::ToolCallPart& c) override;
  void tool_end(const llm::ToolCallPart& c, const tool::Output& o) override;
  void step_end(int, const llm::Usage&, const std::string& model) override;
  void notice(std::string_view n) override;

 private:
  Ui& ui_;
};

class Ui {
 public:
  Ui(cli::App& app, const Options& opts) : app_(app), opts_(opts), bridge_(*this) {}

  int run() {
    auto s = open_session();
    if (!s) return std::cerr << "error: " << s.error().message << "\n", 1;
    session_ = *s;
    commands_ = command::discover(app_.config, app_.root);
    runner_ = std::make_unique<session::Runner>(app_.services([this](const permission::Request& r) { return ask(r); },
                                                              &cancel_, opts_.allow_all,
                                                              [this](const tool::Question& q) { return ask_question(q); }));
    if (opts_.model) model_ = *opts_.model;
    load_history();
    load_transcript();
    if (!term_.ok()) return std::cerr << "not a terminal\n", 1;
    if (opts_.prompt) submit(*opts_.prompt);

    while (!quit_) {
      render();
      auto k = term_.read(busy_ ? 80 : 250);
      if (busy_) ++spin_;
      std::lock_guard lock(mu_);
      if (k.type != KeyType::none) on_key(k);
    }
    if (worker_.joinable()) {
      cancel_ = true;
      worker_.join();
    }
    save_history();
    return 0;
  }

  // ---- called from the worker thread (via Bridge) ----
  void append_text(Block::Kind kind, std::string_view t) {
    std::lock_guard lock(mu_);
    if (blocks_.empty() || blocks_.back().kind != kind) blocks_.push_back({kind, ""});
    blocks_.back().text += t;
    dirty_ = true;
  }
  void add_block(Block b) {
    std::lock_guard lock(mu_);
    blocks_.push_back(std::move(b));
    dirty_ = true;
  }
  void set_model(const std::string& m) {
    std::lock_guard lock(mu_);
    shown_model_ = m;
  }

 private:
  // ---- session ----
  Result<session::Info> open_session() {
    if (opts_.session) return app_.store->get(*opts_.session);
    if (opts_.cont)
      if (auto l = app_.store->latest()) return *l;
    return app_.store->create(opts_.agent.value_or(app_.config.default_agent), opts_.model.value_or(""));
  }

  void load_transcript() {
    blocks_.clear();
    auto ms = app_.store->messages(session_.id);
    if (!ms) return;
    for (auto& m : *ms)
      for (auto& p : m.parts) {
        if (auto* t = std::get_if<llm::TextPart>(&p))
          blocks_.push_back({m.role == llm::Role::user ? Block::user : Block::assistant, t->text});
        else if (auto* r = std::get_if<llm::ToolResultPart>(&p))
          blocks_.push_back({Block::tool, r->title.empty() ? r->name : r->title, r->output, r->is_error, r->diff});
      }
    scroll_ = 0;
  }

  void new_session() {
    auto s = app_.store->create(session_.agent, "");
    if (!s) return add_block({Block::error, s.error().message});
    session_ = *s;
    blocks_.clear();
    runner_ = std::make_unique<session::Runner>(app_.services([this](const permission::Request& r) { return ask(r); },
                                                              &cancel_, opts_.allow_all,
                                                              [this](const tool::Question& q) { return ask_question(q); }));
  }

  // ---- permission dialog ----
  permission::Reply ask(const permission::Request& r) {
    extras::notify(extras::notify_settings(app_.config), "shaman needs permission", r.title);
    std::unique_lock lock(mu_);
    perm_ = r;
    perm_reply_.reset();
    dirty_ = true;
    perm_cv_.wait(lock, [&] { return perm_reply_.has_value() || quit_; });
    perm_.reset();
    return perm_reply_.value_or(permission::Reply::reject);
  }

  // ---- question dialog (reuses the picker; typing a non-matching answer is allowed) ----
  Result<std::string> ask_question(const tool::Question& q) {
    extras::notify(extras::notify_settings(app_.config), "shaman has a question", q.question);
    std::unique_lock lock(mu_);
    std::vector<PickItem> items;
    for (auto& o : q.options) items.push_back({o, "", [this, o] { answer_ = o; }});
    answer_.reset();
    question_open_ = true;
    open_picker("Question: " + q.question.substr(0, 60), std::move(items));
    dirty_ = true;
    perm_cv_.wait(lock, [&] { return !question_open_ || quit_; });
    auto a = answer_;
    answer_.reset();
    if (!a || a->empty()) return fail("the user dismissed the question; make a sensible choice and continue");
    return *a;
  }
  void close_question() {
    if (!question_open_) return;
    question_open_ = false;
    perm_cv_.notify_all();
  }

  // ---- running a turn ----
  void submit(std::string text) {
    text = str::trim(text);
    if (text.empty()) return;
    history_.push_back(text);
    hist_pos_ = history_.size();
    if (text.starts_with("/") && slash(text)) return;
    if (busy_) return add_block({Block::notice, "busy; press Esc to stop the current turn"});
    blocks_.push_back({Block::user, text});
    scroll_ = 0;
    start(text, {});
  }

  void start(const std::string& text, session::PromptOptions po) {
    if (!model_.empty() && !po.model) po.model = model_;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    cancel_ = false;
    worker_ = std::thread([this, text, po] {
      auto info = session_;
      auto t0 = std::chrono::steady_clock::now();
      auto r = runner_->prompt(info, text, bridge_, po);
      auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
      auto ns = extras::notify_settings(app_.config);
      if (secs >= ns.min_seconds && !cancel_) extras::notify(ns, "shaman", r ? "Done: " + info.title : "Error: " + r.error().message);
      std::lock_guard lock(mu_);
      session_ = info;
      if (!r && !cancel_) blocks_.push_back({Block::error, r.error().message});
      if (cancel_) blocks_.push_back({Block::notice, "stopped"});
      busy_ = false;
      dirty_ = true;
    });
  }

  // Returns true when handled as a UI command.
  bool slash(const std::string& line) {
    auto sp = line.find(' ');
    auto cmd = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    auto arg = sp == std::string::npos ? "" : str::trim(line.substr(sp + 1));
    if (cmd == "exit" || cmd == "quit") return quit_ = true;
    if (cmd == "new") return new_session(), true;
    if (cmd == "help") return help(), true;
    if (cmd == "sessions") return pick_session(), true;
    if (cmd == "models") return pick_model(), true;
    if (cmd == "agents") return pick_agent(), true;
    if (cmd == "model") {
      auto m = app_.providers->resolve(arg);
      if (m) model_ = m->ref(), shown_model_ = model_;
      add_block({m ? Block::notice : Block::error, m ? "model: " + m->ref() : m.error().message});
      return true;
    }
    if (cmd == "agent") return set_agent(arg), true;
    if (cmd == "details") return details_ = !details_, true;
    if (cmd == "goal") {
      session_.goal = arg;
      app_.store->save(session_);
      if (arg.empty()) return add_block({Block::notice, "goal cleared"}), true;
      if (busy_) return add_block({Block::notice, "goal set: " + arg}), true;
      blocks_.push_back({Block::user, "/goal " + arg});
      start("Work towards the goal: " + arg, {});
      return true;
    }
    if (cmd == "editor") return editor(), true;
    if (cmd == "revert" || cmd == "fork") {
      if (busy_) return add_block({Block::notice, "busy"}), true;
      bool fork = cmd == "fork";
      std::vector<PickItem> items;
      auto ts = runner_->turns(session_);
      if (fork) items.push_back({"(whole conversation)", "", [this] { do_fork(SIZE_MAX); }});
      for (auto it = ts.rbegin(); it != ts.rend(); ++it) {
        auto label = str::replace_all(it->text.substr(0, 70), "\n", " ");
        items.push_back({std::format("#{} {}", it->index + 1, label), fork ? "fork before this" : "revert to before this",
                         [this, n = it->index, fork] { fork ? do_fork(n) : do_revert(n); }});
      }
      if (items.empty()) return add_block({Block::notice, "no turns to " + cmd + " to"}), true;
      open_picker(fork ? "Fork from..." : "Revert to before...", std::move(items));
      return true;
    }
    if (cmd == "undo" || cmd == "compact" || cmd == "export" || cmd == "share" || cmd == "cost" || cmd == "todos")
      return session_action(cmd, arg), true;
    if (auto* c = command::find(commands_, cmd)) {
      if (busy_) return add_block({Block::notice, "busy"}), true;
      auto text = command::expand(*c, arg, app_.root);
      if (c->agent) session_.agent = *c->agent;
      session::PromptOptions po;
      if (c->model) po.model = c->model;
      blocks_.push_back({Block::user, line});
      start(text, po);
      return true;
    }
    add_block({Block::error, "unknown command /" + cmd + " (Ctrl-P lists everything)"});
    return true;
  }

  void session_action(const std::string& cmd, const std::string& arg) {
    if (busy_ && cmd != "cost" && cmd != "todos") return add_block({Block::notice, "busy"});
    if (cmd == "undo") {
      auto r = runner_->undo(session_);
      return add_block({r ? Block::notice : Block::error, r ? "undone:\n" + *r : r.error().message});
    }
    if (cmd == "compact") {
      session::Events quiet;
      auto r = runner_->compact(session_, quiet);
      if (!r) return add_block({Block::error, r.error().message});
      load_transcript();
      return add_block({Block::notice, "conversation compacted"});
    }
    if (cmd == "cost")
      return add_block({Block::notice, std::format("{} input / {} output tokens ({} cached) · ${:.4f}", session_.usage.input,
                                                   session_.usage.output, session_.usage.cache_read, session_.cost)});
    if (cmd == "todos") {
      std::string out;
      for (auto& t : runner_->todos()) out += "[" + t.status + "] " + t.content + "\n";
      return add_block({Block::notice, out.empty() ? "no todos" : out});
    }
    auto ms = app_.store->messages(session_.id);
    if (!ms) return add_block({Block::error, ms.error().message});
    bool html = cmd == "share" || arg == "html";
    auto ext = html ? ".html" : arg == "json" ? ".json" : ".md";
    auto dir = cmd == "share" ? paths::data_dir() / "shares" : app_.cwd;
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto path = dir / (session_.id + ext);
    std::ofstream out(path);
    out << (html ? session::export_html(session_, *ms) : arg == "json" ? session::export_json(session_, *ms).dump(2)
                                                                       : session::export_markdown(session_, *ms));
    add_block({Block::notice, (cmd == "share" ? "shareable page: " : "exported to ") + path.string()});
  }

  void do_revert(size_t n) {
    auto r = runner_->revert(session_, n);
    if (!r) return add_block({Block::error, r.error().message});
    load_transcript();
    input_ = *r;  // put the reverted message back in the input so it can be edited and re-sent
    cursor_ = input_.size();
    add_block({Block::notice, std::format("reverted to before turn {}; files restored. Edit and press Enter to retry.", n + 1)});
  }
  void do_fork(size_t n) {
    auto r = runner_->fork(session_, n);
    if (!r) return add_block({Block::error, r.error().message});
    session_ = *r;
    load_transcript();
    add_block({Block::notice, "forked into a new session: " + session_.title});
  }

  void set_agent(const std::string& name) {
    auto* a = app_.agents->find(name);
    if (!a || a->mode == agent::Mode::subagent) return add_block({Block::error, "unknown primary agent: " + name});
    session_.agent = name;
    app_.store->save(session_);
  }

  void cycle_agent() {
    std::vector<std::string> primaries;
    for (auto* a : app_.agents->list())
      if (a->mode != agent::Mode::subagent) primaries.push_back(a->name);
    if (primaries.empty()) return;
    auto it = std::ranges::find(primaries, session_.agent);
    set_agent(it == primaries.end() || std::next(it) == primaries.end() ? primaries.front() : *std::next(it));
  }

  void help() {
    add_block({Block::notice,
               "Enter send · Alt/Ctrl-J newline · Tab complete or switch agent · Up/Down history · PgUp/PgDn scroll\n"
               "Esc stop turn · Ctrl-P palette · Ctrl-E external editor · Ctrl-O tool details · Ctrl-C/Ctrl-D quit\n"
               "/new /sessions /models /model <id> /agents /agent <name> /goal <text> /undo /revert /fork /compact\n"
               "/export [md|json|html] /share\n"
               "/cost /todos /details /editor /help /exit, plus custom commands (Ctrl-P)"});
  }

  // ---- pickers ----
  void open_picker(std::string title, std::vector<PickItem> items) {
    picker_title_ = std::move(title);
    picker_items_ = std::move(items);
    picker_filter_.clear();
    picker_sel_ = 0;
  }

  void pick_session() {
    std::vector<PickItem> items;
    for (auto& s : app_.store->list())
      items.push_back({s.title.empty() ? "(untitled) " + s.id : s.title, s.id, [this, s] {
                         session_ = s;
                         load_transcript();
                       }});
    open_picker("Sessions", std::move(items));
  }

  void pick_model() {
    std::vector<PickItem> items;
    for (auto& p : app_.providers->providers()) {
      if (!app_.providers->usable(p)) continue;
      for (auto& m : app_.providers->visible_models(p)) {
        auto ref = p.id + "/" + m.id;
        items.push_back({ref, m.name + (m.free() ? " · free" : ""), [this, ref] { model_ = shown_model_ = ref; }});
      }
    }
    open_picker("Models", std::move(items));
  }

  void pick_agent() {
    std::vector<PickItem> items;
    for (auto* a : app_.agents->list())
      if (a->mode != agent::Mode::subagent) items.push_back({a->name, a->description, [this, n = a->name] { set_agent(n); }});
    open_picker("Agents", std::move(items));
  }

  void palette() {
    std::vector<PickItem> items = {
        {"New session", "/new", [this] { new_session(); }},
        {"Switch session", "/sessions", [this] { pick_session(); }},
        {"Switch model", "/models", [this] { pick_model(); }},
        {"Switch agent", "/agents", [this] { pick_agent(); }},
        {"Undo file changes", "/undo", [this] { session_action("undo", ""); }},
        {"Revert to an earlier message", "/revert", [this] { slash("/revert"); }},
        {"Fork this session", "/fork", [this] { slash("/fork"); }},
        {"Compact conversation", "/compact", [this] { session_action("compact", ""); }},
        {"Export as Markdown", "/export", [this] { session_action("export", ""); }},
        {"Share as HTML page", "/share", [this] { session_action("share", ""); }},
        {"Toggle tool details", "Ctrl-O", [this] { details_ = !details_; }},
        {"Help", "/help", [this] { help(); }},
        {"Quit", "Ctrl-C", [this] { quit_ = true; }},
    };
    for (auto& c : commands_)
      items.push_back({"/" + c.name, c.description, [this, name = c.name] { input_ = "/" + name + " "; cursor_ = input_.size(); }});
    open_picker("Commands", std::move(items));
  }

  std::vector<size_t> filtered() const {
    std::vector<size_t> out;
    auto f = str::lower(picker_filter_);
    for (size_t i = 0; i < picker_items_.size(); ++i)
      if (f.empty() || str::lower(picker_items_[i].label + " " + picker_items_[i].hint).find(f) != std::string::npos) out.push_back(i);
    return out;
  }

  // ---- editing ----
  void insert(const std::string& s) {
    input_.insert(cursor_, s);
    cursor_ += s.size();
  }
  void backspace() {
    if (cursor_ == 0) return;
    size_t start = cursor_ - 1;
    while (start > 0 && (static_cast<unsigned char>(input_[start]) & 0xC0) == 0x80) --start;
    input_.erase(start, cursor_ - start);
    cursor_ = start;
  }
  void left() {
    if (cursor_ == 0) return;
    --cursor_;
    while (cursor_ > 0 && (static_cast<unsigned char>(input_[cursor_]) & 0xC0) == 0x80) --cursor_;
  }
  void right() {
    if (cursor_ >= input_.size()) return;
    ++cursor_;
    while (cursor_ < input_.size() && (static_cast<unsigned char>(input_[cursor_]) & 0xC0) == 0x80) ++cursor_;
  }

  void complete() {
    auto word_start = input_.rfind(' ', cursor_ == 0 ? 0 : cursor_ - 1);
    word_start = word_start == std::string::npos ? 0 : word_start + 1;
    auto word = input_.substr(word_start, cursor_ - word_start);
    std::vector<std::string> options;
    if (word_start == 0 && word.starts_with("/")) {
      for (auto name : {"new", "goal", "revert", "fork", "sessions", "models", "model", "agents", "agent", "undo", "compact", "export", "share",
                        "cost", "todos", "details", "editor", "help", "exit"})
        if (std::string(name).starts_with(word.substr(1))) options.push_back("/" + std::string(name));
      for (auto& c : commands_)
        if (c.name.starts_with(word.substr(1))) options.push_back("/" + c.name);
    } else if (word.starts_with("@")) {
      auto partial = word.substr(1);
      auto dir = paths::resolve(app_.root, fs::path(partial).parent_path());
      auto prefix = fs::path(partial).filename().string();
      std::error_code ec;
      for (auto& e : fs::directory_iterator(dir, ec)) {
        auto name = e.path().filename().string();
        if (!name.starts_with(prefix) || name == ".git") continue;
        auto rel = (fs::path(partial).parent_path() / name).generic_string();
        options.push_back("@" + rel + (e.is_directory(ec) ? "/" : ""));
      }
    } else {
      return cycle_agent();
    }
    if (options.empty()) return;
    std::ranges::sort(options);
    auto common = options.front();
    for (auto& o : options)
      while (!o.starts_with(common)) common.pop_back();
    std::string repl = options.size() == 1 ? options.front() + (options.front().ends_with("/") ? "" : " ") : common;
    input_.replace(word_start, cursor_ - word_start, repl);
    cursor_ = word_start + repl.size();
    if (options.size() > 1) add_block({Block::notice, str::join(options, "  ")});
  }

  void editor() {
    const char* ed = std::getenv("VISUAL");
    if (!ed || !*ed) ed = std::getenv("EDITOR");
    std::string editor = ed && *ed ? ed : "vi";
    auto tmp = fs::temp_directory_path() / ("shaman-" + session_.id + ".md");
    std::ofstream(tmp) << input_;
    term_.write("\x1b[?1049l");
    term_.flush();
    [[maybe_unused]] int rc = std::system((editor + " '" + tmp.string() + "'").c_str());
    term_.write("\x1b[?1049h");
    std::ifstream in(tmp);
    std::stringstream ss;
    ss << in.rdbuf();
    input_ = str::trim(ss.str());
    cursor_ = input_.size();
    std::error_code ec;
    fs::remove(tmp, ec);
  }

  void on_key(const Key& k) {
    dirty_ = true;
    if (k.type == KeyType::resize) return;
    if (perm_) {  // modal permission dialog
      std::optional<permission::Reply> r;
      if (k.type == KeyType::ch && (k.text == "y" || k.text == "Y")) r = permission::Reply::once;
      else if (k.type == KeyType::ch && (k.text == "a" || k.text == "A")) r = permission::Reply::always;
      else if ((k.type == KeyType::ch && (k.text == "n" || k.text == "N")) || k.type == KeyType::esc) r = permission::Reply::reject;
      if (r) {
        perm_reply_ = r;
        perm_cv_.notify_all();
      }
      return;
    }
    if (!picker_items_.empty() || !picker_title_.empty() || question_open_) {  // modal picker
      auto f = filtered();
      if (k.type == KeyType::esc || (k.type == KeyType::ctrl && k.ctrl == 'c')) {
        picker_title_.clear(), picker_items_.clear();
        close_question();
      } else if (k.type == KeyType::enter && question_open_ && (f.empty() || picker_items_.empty())) {
        answer_ = picker_filter_;  // free-form answer typed into the filter box
        picker_title_.clear(), picker_items_.clear();
        close_question();
      }
      else if (k.type == KeyType::up) picker_sel_ = picker_sel_ == 0 ? 0 : picker_sel_ - 1;
      else if (k.type == KeyType::down) picker_sel_ = std::min(picker_sel_ + 1, f.empty() ? 0 : f.size() - 1);
      else if (k.type == KeyType::backspace && !picker_filter_.empty()) picker_filter_.pop_back(), picker_sel_ = 0;
      else if (k.type == KeyType::ch) picker_filter_ += k.text, picker_sel_ = 0;
      else if (k.type == KeyType::enter && !f.empty()) {
        auto action = picker_items_[f[std::min(picker_sel_, f.size() - 1)]].action;
        picker_title_.clear(), picker_items_.clear();
        action();
        close_question();
      }
      return;
    }
    switch (k.type) {
      case KeyType::ch:
        if (k.alt) break;
        insert(k.text);
        break;
      case KeyType::paste: insert(k.text); break;
      case KeyType::enter:
        if (k.alt) insert("\n");
        else if (!input_.empty() && input_.back() == '\\') input_.back() = '\n', cursor_ = input_.size();
        else {
          auto text = input_;
          input_.clear();
          cursor_ = 0;
          submit(text);
        }
        break;
      case KeyType::backspace: backspace(); break;
      case KeyType::del:
        if (cursor_ < input_.size()) right(), backspace();
        break;
      case KeyType::left: left(); break;
      case KeyType::right: right(); break;
      case KeyType::home: cursor_ = input_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1) == std::string::npos ? 0 : input_.rfind('\n', cursor_ - 1) + 1; break;
      case KeyType::end: cursor_ = input_.find('\n', cursor_) == std::string::npos ? input_.size() : input_.find('\n', cursor_); break;
      case KeyType::up:
        if (input_.find('\n') == std::string::npos && hist_pos_ > 0) input_ = history_[--hist_pos_], cursor_ = input_.size();
        break;
      case KeyType::down:
        if (input_.find('\n') == std::string::npos && hist_pos_ < history_.size()) {
          ++hist_pos_;
          input_ = hist_pos_ < history_.size() ? history_[hist_pos_] : "";
          cursor_ = input_.size();
        }
        break;
      case KeyType::pgup: scroll_ += std::max(1, term_.size().rows / 2); break;
      case KeyType::pgdn: scroll_ = std::max(0, scroll_ - std::max(1, term_.size().rows / 2)); break;
      case KeyType::tab: complete(); break;
      case KeyType::shift_tab: cycle_agent(); break;
      case KeyType::esc:
        if (busy_) cancel_ = true;
        else input_.clear(), cursor_ = 0;
        break;
      case KeyType::eof:
        if (input_.empty()) quit_ = true;
        break;
      case KeyType::ctrl:
        switch (k.ctrl) {
          case 'c':
            if (busy_) cancel_ = true;
            else if (!input_.empty()) input_.clear(), cursor_ = 0;
            else quit_ = true;
            break;
          case 'a': cursor_ = 0; break;
          case 'e':
            if (input_.empty() || cursor_ == input_.size()) editor();
            else cursor_ = input_.size();
            break;
          case 'k': input_.erase(cursor_); break;
          case 'u': input_.erase(0, cursor_), cursor_ = 0; break;
          case 'w': {
            auto start = input_.find_last_not_of(' ', cursor_ == 0 ? 0 : cursor_ - 1);
            start = start == std::string::npos ? 0 : input_.rfind(' ', start);
            start = start == std::string::npos ? 0 : start + 1;
            input_.erase(start, cursor_ - start);
            cursor_ = start;
            break;
          }
          case 'p': palette(); break;
          case 'o': details_ = !details_; break;
          case 'l': term_.write("\x1b[2J"); break;
          case 'n': new_session(); break;
          default: break;
        }
        break;
      default: break;
    }
  }

  // ---- drawing ----
  std::vector<std::string> transcript(size_t width) {
    std::vector<std::string> out;
    for (auto& b : blocks_) {
      switch (b.kind) {
        case Block::user: {
          out.push_back("");
          for (auto& l : wrap(std::string(BOLD) + b.text, width - 2)) out.push_back(std::string(ACCENT) + glyph("▌ ", "| ") + R + l);
          out.push_back("");
          break;
        }
        case Block::assistant:
          for (auto& l : render_markdown(b.text, width)) out.push_back(l);
          break;
        case Block::reasoning:
          for (auto& l : wrap(std::string(DIM) + "\x1b[3m" + b.text, width, "  ")) out.push_back(l);
          break;
        case Block::tool: {
          auto line = std::string(b.failed ? RED : CYAN) + (b.failed ? glyph("✗ ", "x ") : glyph("› ", "> ")) + R + DIM + b.text + R;
          out.push_back(line);
          if (!b.diff.empty()) {  // edits show their diff inline; Ctrl-O shows all of it
            auto ls = str::lines(b.diff);
            size_t shown = 0, limit = details_ ? 400 : 24;
            for (auto& l : ls) {
              if (l.starts_with("---") || l.starts_with("+++")) continue;
              if (++shown > limit) {
                out.push_back(std::format("    {}... {} more lines (Ctrl-O){}", DIM, ls.size() - shown, R));
                break;
              }
              const char* c = l.starts_with("+") ? "\x1b[32m" : l.starts_with("-") ? "\x1b[31m" : l.starts_with("@@") ? CYAN : DIM;
              out.push_back("    " + std::string(c) + clip(l, width - 4) + R);
            }
          }
          if (details_ && !b.detail.empty() && b.diff.empty()) {
            auto ls = str::lines(b.detail);
            for (size_t i = 0; i < ls.size() && i < 12; ++i)
              for (auto& l : wrap(std::string(DIM) + ls[i], width - 4)) out.push_back("    " + l);
            if (ls.size() > 12) out.push_back(std::format("    {}... {} more lines{}", DIM, ls.size() - 12, R));
          }
          break;
        }
        case Block::notice:
          for (auto& l : wrap(std::string(YELLOW) + b.text, width)) out.push_back(l);
          break;
        case Block::error:
          for (auto& l : wrap(std::string(RED) + "error: " + b.text, width)) out.push_back(l);
          break;
      }
    }
    return out;
  }

  void render() {
    std::lock_guard lock(mu_);
    if (!dirty_ && !busy_) return;
    dirty_ = false;
    auto sz = term_.size();
    size_t width = std::max(20, sz.cols - 2);

    // Input box height grows with content, up to 8 lines.
    auto input_lines = str::split(input_, '\n');
    std::vector<std::string> in_rows;
    for (size_t i = 0; i < input_lines.size(); ++i)
      for (auto& l : wrap(input_lines[i], width - 2)) in_rows.push_back(l);
    if (in_rows.empty()) in_rows.push_back("");
    int in_h = std::min<int>(int(in_rows.size()), 8);
    int body_h = std::max(3, sz.rows - in_h - 3);  // header, status line, border

    auto lines = transcript(width);
    int max_scroll = std::max(0, int(lines.size()) - body_h);
    scroll_ = std::min(scroll_, max_scroll);
    int first = std::max(0, int(lines.size()) - body_h - scroll_);

    // Every row is placed explicitly and clipped to the width, so nothing can
    // wrap or scroll the screen whatever the terminal thinks a glyph's width is.
    std::string out = "\x1b[?25l";
    int row = 0;
    auto put = [&](const std::string& line) {
      out += std::format("\x1b[{};1H", ++row) + clip(line, size_t(sz.cols)) + R + "\x1b[K";
    };
    auto* agent = app_.agents->find(session_.agent);
    std::string model = !shown_model_.empty() ? shown_model_ : !model_.empty() ? model_ : session_.model;
    if (model.empty())
      if (auto d = app_.providers->default_model()) model = d->ref();
    auto dot = glyph(" · ", " | ");
    put(std::format(" {}shaman{}{}{}{}{}{}{}{}{}", BOLD, R, DIM, dot, R + std::string(ACCENT), agent ? agent->name : session_.agent,
                    R + std::string(DIM) + dot, model, dot, (session_.title.empty() ? std::string("new session") : session_.title) + R));
    for (int i = 0; i < body_h; ++i) {
      int idx = first + i;
      put(" " + (idx < int(lines.size()) ? lines[idx] : ""));
    }
    static const char* spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    static const char* ascii_spinner[] = {"|", "/", "-", "\\"};
    std::string spin = unicode() ? spinner[spin_ % 10] : ascii_spinner[spin_ % 4];
    std::string status = busy_ ? std::format(" {}{} working{} {}esc to stop{}", ACCENT, spin, R, DIM, R)
                               : std::format(" {}enter send{}tab agent{}ctrl-p commands{}", DIM, dot, dot, R);
    auto right_status = std::format("{}{}{} in{}{} out{}${:.4f}{}{} ", DIM, scroll_ ? std::format("{}{}{}", glyph("↑", "^"), scroll_, dot) : "",
                                    session_.usage.input, dot, session_.usage.output, dot, session_.cost, details_ ? dot + "details" : "", R);
    int pad = sz.cols - 1 - int(display_width(status)) - int(display_width(right_status));  // never touch the last column
    put(status + std::string(size_t(std::max(1, pad)), ' ') + right_status);
    put(std::string(DIM) + std::string(size_t(sz.cols - 1), '-'));

    // Input rows; compute the cursor's row/col.
    int start_row = std::max(0, int(in_rows.size()) - in_h);
    for (int i = 0; i < in_h; ++i)
      put((i == 0 && start_row == 0 ? std::string(ACCENT) + glyph("› ", "> ") + R : "  ") + in_rows[start_row + i]);
    auto before = input_.substr(0, cursor_);
    auto before_lines = str::split(before, '\n');
    int crow = 0, ccol = 0;
    for (size_t i = 0; i < before_lines.size(); ++i) {
      auto w = int(display_width(before_lines[i]));
      int wraps = w / int(width - 2);
      if (i + 1 < before_lines.size()) crow += wraps + 1;
      else crow += wraps, ccol = w % int(width - 2);
    }
    int input_top = sz.rows - in_h + 1;
    int cur_row = input_top + std::clamp(crow - start_row, 0, in_h - 1);

    if (perm_) out += overlay_permission(sz);
    else if (!picker_title_.empty() || question_open_) out += overlay_picker(sz);
    else out += std::format("\x1b[{};{}H\x1b[?25h", cur_row, ccol + 3);
    term_.write(out);
    term_.flush();
  }

  std::string box(Size sz, int w, int h, const std::string& title, const std::vector<std::string>& rows) {
    int top = std::max(1, (sz.rows - h) / 2), left = std::max(1, (sz.cols - w) / 2);
    std::string out = std::format("\x1b[{};{}H{}{}{}{}{} {}{}{}", top, left, ACCENT, glyph("╭─ ", "+- "), BOLD, title, R + std::string(ACCENT),
                                  std::string(size_t(std::max(0, w - 5 - int(display_width(title)))), '-'), glyph("╮", "+"), R);
    for (int i = 0; i < h - 2; ++i) {
      auto row = i < int(rows.size()) ? rows[i] : "";
      int padn = w - 4 - int(display_width(row));
      out += std::format("\x1b[{};{}H{}{}{} {}{} {}{}{}", top + 1 + i, left, ACCENT, glyph("│", "|"), R, clip(row, size_t(w - 4)),
                         std::string(size_t(std::max(0, padn)), ' '), ACCENT, glyph("│", "|"), R);
    }
    out += std::format("\x1b[{};{}H{}{}{}{}{}", top + h - 1, left, ACCENT, glyph("╰", "+"), std::string(size_t(w - 2), '-'), glyph("╯", "+"), R);
    return out;
  }

  std::string overlay_permission(Size sz) {
    int w = std::min(sz.cols - 4, 76);
    std::vector<std::string> rows;
    for (auto& l : wrap(perm_->title, size_t(w - 4))) rows.push_back(l);
    rows.push_back("");
    rows.push_back(std::format("{}y{} allow once   {}a{} always this session   {}n{} deny", BOLD, R, BOLD, R, BOLD, R));
    return box(sz, w, int(rows.size()) + 2, "permission: " + perm_->permission, rows) + "\x1b[?25l";
  }

  std::string overlay_picker(Size sz) {
    int w = std::min(sz.cols - 4, 80), h = std::min(sz.rows - 4, 18);
    auto f = filtered();
    std::vector<std::string> rows{std::format("{}filter:{} {}", DIM, R, picker_filter_), ""};
    size_t visible = size_t(h - 4);
    size_t top = picker_sel_ >= visible ? picker_sel_ - visible + 1 : 0;
    for (size_t i = top; i < f.size() && rows.size() < size_t(h - 2); ++i) {
      auto& it = picker_items_[f[i]];
      auto label = it.label.substr(0, size_t(w / 2));
      auto hint = it.hint.substr(0, size_t(std::max(0, w - 8 - int(display_width(label)))));
      rows.push_back(i == picker_sel_ ? std::string(INVERT) + " " + label + " " + R + " " + DIM + hint + R
                                      : " " + label + "  " + DIM + hint + R);
    }
    if (f.empty()) rows.push_back(std::string(DIM) + (question_open_ ? " type an answer, then Enter" : " no matches") + R);
    return box(sz, w, h, picker_title_, rows) + "\x1b[?25l";
  }

  // ---- history ----
  fs::path history_file() const { return paths::data_dir() / "history"; }
  void load_history() {
    std::ifstream in(history_file());
    std::string line;
    while (std::getline(in, line))
      if (!line.empty()) history_.push_back(str::replace_all(line, "\\n", "\n"));
    if (history_.size() > 1000) history_.erase(history_.begin(), history_.end() - 1000);
    hist_pos_ = history_.size();
  }
  void save_history() {
    std::error_code ec;
    fs::create_directories(history_file().parent_path(), ec);
    std::ofstream out(history_file(), std::ios::trunc);
    size_t start = history_.size() > 1000 ? history_.size() - 1000 : 0;
    for (size_t i = start; i < history_.size(); ++i) out << str::replace_all(history_[i], "\n", "\\n") << "\n";
  }

  cli::App& app_;
  Options opts_;
  Terminal term_;
  Bridge bridge_;
  std::unique_ptr<session::Runner> runner_;
  session::Info session_;
  std::vector<command::Command> commands_;
  std::string model_, shown_model_;

  std::recursive_mutex mu_;
  std::condition_variable_any perm_cv_;
  std::vector<Block> blocks_;
  std::optional<permission::Request> perm_;
  std::optional<permission::Reply> perm_reply_;
  std::optional<std::string> answer_;
  bool question_open_ = false;
  std::string picker_title_, picker_filter_;
  std::vector<PickItem> picker_items_;
  size_t picker_sel_ = 0;

  std::string input_;
  size_t cursor_ = 0;
  std::vector<std::string> history_;
  size_t hist_pos_ = 0;
  int scroll_ = 0;
  bool details_ = false, dirty_ = true, quit_ = false;
  std::atomic<bool> busy_{false}, cancel_{false};
  unsigned spin_ = 0;
  std::thread worker_;

  friend class Bridge;
};

void Bridge::text(std::string_view t) { ui_.append_text(Block::assistant, t); }
void Bridge::reasoning(std::string_view t) { ui_.append_text(Block::reasoning, t); }
void Bridge::tool_start(const llm::ToolCallPart&) {}
void Bridge::tool_end(const llm::ToolCallPart&, const tool::Output& o) { ui_.add_block({Block::tool, o.title, o.text, o.is_error, o.diff}); }
void Bridge::step_end(int, const llm::Usage&, const std::string& model) { ui_.set_model(model); }
void Bridge::notice(std::string_view n) { ui_.add_block({Block::notice, std::string(n)}); }

}  // namespace

int run(cli::App& app, const Options& opts) { return Ui(app, opts).run(); }

}  // namespace shaman::tui
