#include "shaman/github/github.hpp"

#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

#include "shaman/cli/render.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"

namespace shaman::github {
namespace fs = std::filesystem;

namespace {

constexpr const char* kWorkflow = R"(name: shaman

on:
  issue_comment:
    types: [created]
  pull_request_review_comment:
    types: [created]

jobs:
  shaman:
    if: contains(github.event.comment.body, '/shaman')
    runs-on: ubuntu-latest
    permissions:
      contents: write
      pull-requests: write
      issues: write
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - name: Install shaman
        run: curl -fsSL https://github.com/nikolas-chambers/shaman-cli/releases/latest/download/shaman-linux-x64 -o /usr/local/bin/shaman && chmod +x /usr/local/bin/shaman
      - name: Run shaman
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          # Optional: a paid model instead of the free tier
          # SHAMAN_MODEL: anthropic/claude-sonnet-5
          # ANTHROPIC_API_KEY: ${{ secrets.ANTHROPIC_API_KEY }}
        run: shaman github run
)";

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

struct Api {
  std::string repo, token, base = env("GITHUB_API_URL").empty() ? "https://api.github.com" : env("GITHUB_API_URL");
  Result<Json> call(const std::string& method, const std::string& path, const Json& body = nullptr) const {
    http::Request req;
    req.method = method;
    req.url = base + path;
    req.headers = {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"},
                   {"Authorization", "Bearer " + token}, {"Content-Type", "application/json"}};
    if (!body.is_null()) req.body = body.dump();
    auto res = http::send(req);
    if (!res) return std::unexpected(res.error());
    if (res->status >= 300) return fail(std::format("GitHub {} {} -> HTTP {}: {}", method, path, res->status, res->body.substr(0, 300)));
    if (res->body.empty()) return Json::object();
    return Json::parse(res->body);
  }
  Result<std::string> diff(int pr) const {
    http::Request req;
    req.url = std::format("{}/repos/{}/pulls/{}", base, repo, pr);
    req.headers = {{"Accept", "application/vnd.github.v3.diff"}, {"Authorization", "Bearer " + token}};
    auto res = http::send(req);
    if (!res) return std::unexpected(res.error());
    return res->body;
  }
};

Result<std::string> git(const fs::path& root, const std::string& args) {
  auto r = process::shell("git " + args, {.cwd = root, .timeout = std::chrono::minutes(2)});
  if (!r) return std::unexpected(r.error());
  if (r->exit_code != 0) return fail("git " + args + ": " + str::trim(r->output));
  return str::trim(r->output);
}

}  // namespace

std::string extract_prompt(const std::string& comment, const std::string& trigger) {
  auto pos = comment.find(trigger);
  if (pos == std::string::npos) return "";
  if (pos > 0 && !std::isspace(static_cast<unsigned char>(comment[pos - 1]))) return "";
  auto rest = str::trim(comment.substr(pos + trigger.size()));
  return rest.empty() ? "Look at this and do what's needed." : rest;
}

std::string branch_name(const std::string& kind, int number, const std::string& session_id) {
  return std::format("shaman/{}{}-{}", kind, number, session_id.substr(session_id.size() - 6));
}

int install(const fs::path& root) {
  auto path = root / ".github" / "workflows" / "shaman.yml";
  std::error_code ec;
  if (fs::exists(path, ec)) return std::cerr << path.string() << " already exists\n", 1;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream(path) << kWorkflow;
  std::cout << "Wrote " << path.lexically_relative(root).string()
            << "\nCommit it, then comment \"/shaman <request>\" on any issue or pull request.\n";
  return 0;
}

int run(cli::App& app, bool dry_run) {
  auto event_path = env("GITHUB_EVENT_PATH");
  if (event_path.empty()) return std::cerr << "shaman github run: GITHUB_EVENT_PATH not set (run inside GitHub Actions)\n", 2;
  Json ev;
  try {
    std::ifstream in(event_path);
    ev = Json::parse(in);
  } catch (const std::exception& e) {
    return std::cerr << "cannot read event: " << e.what() << "\n", 1;
  }
  Api api{env("GITHUB_REPOSITORY"), env("GITHUB_TOKEN")};
  auto trigger = env("SHAMAN_TRIGGER").empty() ? "/shaman" : env("SHAMAN_TRIGGER");
  auto comment = ev.value("comment", Json::object());
  auto request = extract_prompt(comment.value("body", ""), trigger);
  if (request.empty()) return std::cout << "no " << trigger << " trigger; nothing to do\n", 0;

  bool is_pr = ev.contains("pull_request") || (ev.contains("issue") && ev["issue"].contains("pull_request"));
  auto issue = ev.contains("issue") ? ev["issue"] : ev.value("pull_request", Json::object());
  int number = issue.value("number", 0);
  auto author = comment.value("user", Json::object()).value("login", "someone");

  // Only people with write access may drive the agent.
  if (!dry_run) {
    auto perm = api.call("GET", std::format("/repos/{}/collaborators/{}/permission", api.repo, author));
    auto level = perm ? perm->value("permission", "none") : "none";
    if (level != "admin" && level != "write" && level != "maintain")
      return std::cerr << author << " lacks write access; ignoring\n", 0;
  }

  std::string context = std::format("You were asked on GitHub {} #{} (\"{}\") by @{}:\n\n{}\n\n{} description:\n{}\n",
                                    is_pr ? "pull request" : "issue", number, issue.value("title", ""), author, request,
                                    is_pr ? "PR" : "Issue", issue.value("body", "") .empty() ? "(none)" : issue.value("body", ""));
  if (comment.contains("path"))
    context += std::format("\nThe comment is on {} line {}:\n```\n{}\n```\n", comment.value("path", ""),
                           comment.value("line", 0), comment.value("diff_hunk", ""));
  if (is_pr && !dry_run)
    if (auto d = api.diff(number)) context += "\nPR diff:\n```diff\n" + d->substr(0, 60'000) + "\n```\n";
  context += "\nMake the changes in this checkout if code changes are needed; do not commit or push, shaman does that. "
             "Finish with a short summary for the GitHub reply.";

  std::string pr_branch;
  if (is_pr && !dry_run) {
    if (auto pr = api.call("GET", std::format("/repos/{}/pulls/{}", api.repo, number))) {
      pr_branch = (*pr)["head"].value("ref", "");
      git(app.root, "fetch origin " + pr_branch);
      git(app.root, "checkout " + pr_branch);
    }
  }

  auto s = app.store->create(app.config.default_agent, "");
  if (!s) return std::cerr << s.error().message << "\n", 1;
  session::Runner runner(app.services(nullptr, nullptr, /*allow_all=*/true));  // Actions runners are disposable
  cli::TerminalEvents events;
  auto answer = runner.prompt(*s, context, events);
  events.finish();
  std::string reply = answer ? *answer : "shaman hit an error: " + answer.error().message;

  auto status = git(app.root, "status --porcelain");
  bool changed = status && !status->empty();
  if (changed && !dry_run) {
    git(app.root, "config user.name 'shaman[bot]'");
    git(app.root, "config user.email 'shaman-bot@users.noreply.github.com'");
    git(app.root, "add -A");
    auto msg = std::format("shaman: {}\n\nRequested by @{} in #{}", request.substr(0, 60), author, number);
    std::ofstream(app.root / ".git" / "SHAMAN_MSG") << msg;
    git(app.root, "commit -q -F .git/SHAMAN_MSG");
    if (is_pr && !pr_branch.empty()) {
      auto pushed = git(app.root, "push origin HEAD:" + pr_branch);
      reply += pushed ? "\n\nPushed the changes to this PR." : "\n\nCould not push: " + pushed.error().message;
    } else {
      auto branch = branch_name("issue", number, s->id);
      auto base = git(app.root, "rev-parse --abbrev-ref HEAD").value_or("main");
      if (auto pushed = git(app.root, "push origin HEAD:refs/heads/" + branch); pushed) {
        auto pr = api.call("POST", std::format("/repos/{}/pulls", api.repo),
                           {{"title", "shaman: " + issue.value("title", request.substr(0, 60))}, {"head", branch}, {"base", base},
                            {"body", std::format("Closes #{}\n\n{}", number, reply)}});
        reply += pr ? "\n\nOpened " + pr->value("html_url", "a pull request") + "." : "\n\nPushed `" + branch + "` but could not open a PR.";
      } else {
        reply += "\n\nCould not push: " + pushed.error().message;
      }
    }
  }
  reply += std::format("\n\n<sub>shaman · {} · {} in / {} out tokens</sub>", s->model, s->usage.input, s->usage.output);
  if (dry_run) return std::cout << "\n--- reply ---\n" << reply << "\n", 0;
  auto posted = api.call("POST", std::format("/repos/{}/issues/{}/comments", api.repo, number), {{"body", reply}});
  if (!posted) return std::cerr << posted.error().message << "\n", 1;
  return answer ? 0 : 1;
}

}  // namespace shaman::github
