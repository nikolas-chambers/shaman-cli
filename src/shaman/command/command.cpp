#include "shaman/command/command.hpp"

#include <fstream>
#include <map>
#include <regex>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::command {
namespace fs = std::filesystem;

namespace {

void builtins(std::map<std::string, Command>& out) {
  out["init"] = {"init", "Create or update AGENTS.md for this project",
                 "Analyse this codebase and create an AGENTS.md file in the project root (or improve the existing one). "
                 "Include: build, lint and test commands (especially how to run a single test); code style: imports, "
                 "formatting, naming, error handling; and anything a new contributor would get wrong. Keep it around "
                 "20-30 lines. If the repo has other agent or editor instruction files, fold in what matters.\n\n$ARGUMENTS",
                 std::nullopt, std::nullopt, "builtin"};
  out["review"] = {"review", "Review uncommitted changes (or a ref: /review main)",
                   "Review these changes for bugs, security problems and unclear code. Report only real issues, most "
                   "severe first, each with path:line and a concrete fix. Say so if everything looks fine.\n\n"
                   "!`git diff ${ARGUMENTS:-HEAD} --stat`\n\n!`git diff ${ARGUMENTS:-HEAD}`",
                   std::nullopt, std::nullopt, "builtin"};
}

}  // namespace

std::vector<Command> discover(const Config& config, const fs::path& root) {
  std::map<std::string, Command> found;
  builtins(found);
  for (auto dir : {paths::config_dir() / "commands", root / ".shaman" / "commands"}) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
      if (e.path().extension() != ".md") continue;
      std::ifstream in(e.path());
      std::stringstream ss;
      ss << in.rdbuf();
      auto [fields, body] = str::front_matter(ss.str());
      // Nested folders become namespaced commands: commands/git/commit.md -> git:commit
      auto rel = e.path().lexically_relative(dir).replace_extension().generic_string();
      Command c{str::replace_all(rel, "/", ":"), "", str::trim(body), std::nullopt, std::nullopt, e.path().string()};
      for (auto& [k, v] : fields) {
        if (k == "description") c.description = v;
        if (k == "agent") c.agent = v;
        if (k == "model") c.model = v;
      }
      found[c.name] = std::move(c);
    }
  }
  auto configured = config.raw.value("command", Json::object());  // named: items() must not outlive it
  for (auto& [name, spec] : configured.items()) {
    Command c{name, spec.value("description", ""), spec.value("template", ""), std::nullopt, std::nullopt, "config"};
    if (spec.contains("agent")) c.agent = spec["agent"].get<std::string>();
    if (spec.contains("model")) c.model = spec["model"].get<std::string>();
    found[name] = std::move(c);
  }
  std::vector<Command> out;
  for (auto& [_, c] : found) {
    log::debug(log::Cat::agent, "command /{} from {}", c.name, c.source);
    out.push_back(std::move(c));
  }
  return out;
}

const Command* find(const std::vector<Command>& commands, const std::string& name) {
  for (auto& c : commands)
    if (c.name == name) return &c;
  return nullptr;
}

std::string expand(const Command& cmd, const std::string& arguments, const fs::path& root) {
  std::string out = cmd.body;
  auto words = str::split(str::trim(arguments), ' ');
  for (int i = 9; i >= 1; --i)
    out = str::replace_all(out, "$" + std::to_string(i), size_t(i) <= words.size() ? words[i - 1] : "");
  // Shell first, so ${ARGUMENTS:-default} inside !`...` is expanded by the shell with ARGUMENTS in its env.
  static const std::regex shell(R"(!`([^`]+)`)");
  std::string result;
  size_t last = 0;
  for (auto it = std::sregex_iterator(out.begin(), out.end(), shell); it != std::sregex_iterator(); ++it) {
    result += out.substr(last, it->position() - last);
    std::string command = "ARGUMENTS=" + std::string("'") + str::replace_all(arguments, "'", "'\\''") + "'; " + (*it)[1].str();
    auto r = process::shell(command, {.cwd = root, .timeout = std::chrono::seconds(30)});
    result += r ? str::trim(r->output) : "(command failed: " + r.error().message + ")";
    last = it->position() + it->length();
  }
  result += out.substr(last);
  return str::replace_all(result, "$ARGUMENTS", arguments);
}

}  // namespace shaman::command
