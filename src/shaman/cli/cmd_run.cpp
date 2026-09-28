// run, run --attach, and the line-based interface.
#include <format>
#include <iostream>
#include <iterator>

#include "shaman/cli/commands.hpp"
#include "shaman/cli/render.hpp"
#include "shaman/command/command.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"
#include "shaman/http/sse.hpp"
#include "shaman/version.hpp"

namespace shaman::cli {

Result<session::Info> open_session(App& app, const Options& o) {
  if (o.session) return app.store->get(*o.session);
  if (o.cont)
    if (auto latest = app.store->latest()) return *latest;
  auto agent = o.agent.value_or(app.config.default_agent);
  auto* a = app.agents->find(agent);
  if (!a || a->mode == agent::Mode::subagent) return fail("unknown primary agent: " + agent);
  return app.store->create(agent, o.model.value_or(""));
}

static std::string read_stdin_if_piped(std::string message) {
  if (!stdin_is_tty()) {
    std::string piped(std::istreambuf_iterator<char>(std::cin), {});
    if (!piped.empty()) message = message.empty() ? piped : message + "\n\n" + piped;
  }
  return message;
}

int cmd_run(App& app, const Options& o, std::string message) {
  message = read_stdin_if_piped(std::move(message));
  auto s = open_session(app, o);
  if (!s) return std::cerr << "error: " << s.error().message << "\n", 1;
  if (o.agent) s->agent = *o.agent;
  session::PromptOptions po;
  po.model = o.model;
  po.attachments = o.files;
  if (o.command) {
    auto commands = command::discover(app.config, app.root);
    auto* c = command::find(commands, *o.command);
    if (!c) return std::cerr << "unknown command: " << *o.command << "\n", 2;
    message = command::expand(*c, message, app.root);
    if (c->agent) s->agent = *c->agent;
    if (c->model && !po.model) po.model = c->model;
  }
  if (str::trim(message).empty()) return std::cerr << "shaman run: no message\n", 2;

  session::Runner runner(app.services(ask_terminal, &g_cancel, o.yolo));
  TerminalEvents term(o.reasoning);
  JsonEvents json;
  session::Events& ev = o.json ? static_cast<session::Events&>(json) : term;
  g_busy = true;
  auto r = runner.prompt(*s, message, ev, po);
  g_busy = false;
  term.finish();
  if (!r) return std::cerr << "error: " << r.error().message << "\n", 1;
  return 0;
}

int cmd_attach(const Options& o, std::string message) {
  message = read_stdin_if_piped(std::move(message));
  auto base = *o.attach;
  while (base.ends_with("/")) base.pop_back();
  const char* token = std::getenv("SHAMAN_SERVER_TOKEN");
  auto headers = [&] {
    std::vector<std::pair<std::string, std::string>> h{{"Content-Type", "application/json"}};
    if (token && *token) h.emplace_back("Authorization", std::string("Bearer ") + token);
    return h;
  };
  std::string sid = o.session.value_or("");
  if (sid.empty()) {
    http::Request req{"POST", base + "/session", headers(), Json{{"agent", o.agent.value_or("build")}}.dump()};
    auto res = http::send(req);
    if (!res || res->status >= 300) return std::cerr << "cannot create session on " << base << "\n", 1;
    sid = Json::parse(res->body).value("id", "");
  }
  Json body = {{"text", message}};
  if (o.model) body["model"] = *o.model;
  if (!o.files.empty()) body["files"] = o.files;
  http::Request req{"POST", base + "/session/" + sid + "/message", headers(), body.dump()};
  http::SseParser sse;
  TerminalEvents term(o.reasoning);
  int code = 0;
  auto res = http::stream(req, [&](std::string_view chunk) {
    sse.feed(chunk, [&](const http::SseEvent& e) {
      auto d = Json::parse(e.data);
      if (o.json) return void(std::cout << Json{{"type", e.event}, {"data", d}}.dump() << "\n" << std::flush);
      if (e.event == "text") term.text(d.value("text", ""));
      else if (e.event == "reasoning") term.reasoning(d.value("text", ""));
      else if (e.event == "tool_end") term.tool_end({d.value("id", ""), d.value("name", ""), Json::object()},
                                                    {d.value("output", ""), d.value("is_error", false), d.value("title", "")});
      else if (e.event == "notice") term.notice(d.value("text", ""));
      else if (e.event == "error") std::cerr << "error: " << d.value("message", "") << "\n", code = 1;
      else if (e.event == "permission") {
        auto reply = ask_terminal({d.value("permission", ""), d.value("subject", ""), d.value("title", "")});
        std::string r = reply == permission::Reply::always ? "always" : reply == permission::Reply::once ? "once" : "reject";
        http::send({"POST", base + "/permission/" + d.value("id", ""), headers(), Json{{"reply", r}}.dump()});
      }
    });
    return true;
  });
  term.finish();
  if (!res) return std::cerr << "error: " << res.error().message << "\n", 1;
  if (res->status >= 300) return std::cerr << "error: HTTP " << res->status << " " << res->body << "\n", 1;
  return code;
}

int cmd_repl(App& app, Options o) {
  auto s = open_session(app, o);
  if (!s) return std::cerr << "error: " << s.error().message << "\n", 1;
  session::Runner runner(app.services(ask_terminal, &g_cancel, o.yolo));
  auto commands = command::discover(app.config, app.root);
  bool color = stdout_is_tty();
  auto model = app.providers->resolve(o.model.value_or(s->model));
  if (!model) model = app.providers->default_model();
  std::cout << std::format("shaman {} | agent {} | model {} | session {}\n/help for commands, ^C stops a turn, ^D quits\n",
                           kVersion, s->agent, model ? model->ref() : "?", s->id);
  std::string line;
  while (true) {
    std::cout << (color ? "\x1b[1m> \x1b[0m" : "> ") << std::flush;
    if (!std::getline(std::cin, line)) break;
    line = str::trim(line);
    if (line.empty()) continue;
    session::PromptOptions po;
    po.model = o.model;
    if (line.starts_with("/")) {
      auto sp = line.find(' ');
      auto cmd = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
      auto arg = sp == std::string::npos ? "" : str::trim(line.substr(sp + 1));
      if (cmd == "exit" || cmd == "quit") break;
      if (cmd == "help") {
        std::cout << "/new /sessions /agent <name> /model <ref> /models /undo /compact /todos /cost /exit";
        for (auto& c : commands) std::cout << " /" << c.name;
        std::cout << "\n";
        continue;
      }
      if (cmd == "new") {
        if (auto n = app.store->create(s->agent, s->model)) s = n, std::cout << "new session " << s->id << "\n";
        continue;
      }
      if (cmd == "sessions") {
        for (auto& i : app.store->list()) std::cout << "  " << i.id << "  " << i.title << "\n";
        continue;
      }
      if (cmd == "agent") {
        auto* a = app.agents->find(arg);
        if (a && a->mode != agent::Mode::subagent) s->agent = arg, std::cout << "agent: " << arg << "\n";
        else std::cout << "unknown primary agent\n";
        continue;
      }
      if (cmd == "model") {
        auto m = app.providers->resolve(arg);
        if (m) o.model = m->ref(), std::cout << "model: " << m->ref() << "\n";
        else std::cout << m.error().message << "\n";
        continue;
      }
      if (cmd == "models") {
        print_models(app, false);
        continue;
      }
      if (cmd == "undo") {
        auto r = runner.undo(*s);
        std::cout << (r ? *r : r.error().message) << "\n";
        continue;
      }
      if (cmd == "compact") {
        TerminalEvents ev;
        if (auto r = runner.compact(*s, ev); !r) std::cout << r.error().message << "\n";
        continue;
      }
      if (cmd == "todos") {
        for (auto& t : runner.todos()) std::cout << "  [" << t.status << "] " << t.content << "\n";
        continue;
      }
      if (cmd == "cost") {
        std::cout << std::format("tokens in {} out {} (cached {}), ${:.4f}\n", s->usage.input, s->usage.output, s->usage.cache_read, s->cost);
        continue;
      }
      auto* c = command::find(commands, cmd);
      if (!c) {
        std::cout << "unknown command; /help\n";
        continue;
      }
      line = command::expand(*c, arg, app.root);
      if (c->model) po.model = c->model;
    }
    TerminalEvents ev(o.reasoning);
    g_cancel = false;
    g_busy = true;
    auto r = runner.prompt(*s, line, ev, po);
    g_busy = false;
    ev.finish();
    if (g_cancel) std::cout << "(stopped)\n";
    else if (!r) std::cerr << "error: " << r.error().message << "\n";
  }
  return 0;
}

}  // namespace shaman::cli
