#include "shaman/extras/schedule.hpp"

#include <fstream>
#include <format>
#include <regex>

#include "shaman/core/id.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::extras {
namespace fs = std::filesystem;

bool valid_cron(const std::string& expr) {
  static const std::regex field(R"((\*|\d+(-\d+)?)(\/\d+)?(,(\*|\d+(-\d+)?)(\/\d+)?)*|[A-Za-z]{3}(-[A-Za-z]{3})?)");
  auto parts = str::split(str::trim(expr), ' ');
  std::erase_if(parts, [](const std::string& s) { return s.empty(); });
  if (parts.size() == 1 && parts[0].starts_with("@")) return true;  // @daily, @hourly, ...
  if (parts.size() != 5) return false;
  for (auto& p : parts)
    if (!std::regex_match(p, field)) return false;
  return true;
}

static std::string sh_quote(const std::string& s) { return "'" + str::replace_all(s, "'", "'\\''") + "'"; }

static Result<std::string> read_crontab() {
  if (!process::which("crontab")) return fail("crontab is not available on this system");
  auto r = process::run({"crontab", "-l"}, {.timeout = std::chrono::seconds(10)});
  if (!r) return std::unexpected(r.error());
  return r->exit_code == 0 ? r->output : std::string();  // "no crontab for user" -> empty
}

static Result<void> write_crontab(const std::string& content) {
  auto tmp = fs::temp_directory_path() / ("shaman-cron-" + make_id("c"));
  std::ofstream(tmp) << content;
  auto r = process::run({"crontab", tmp.string()}, {.timeout = std::chrono::seconds(10)});
  std::error_code ec;
  fs::remove(tmp, ec);
  if (!r) return std::unexpected(r.error());
  if (r->exit_code != 0) return fail("crontab: " + str::trim(r->output));
  return {};
}

Result<std::string> schedule_add(const fs::path& root, const std::string& cron, const std::string& prompt, const std::string& extra) {
  if (!valid_cron(cron)) return fail("invalid cron expression (5 fields, e.g. \"0 9 * * 1-5\")");
  auto current = read_crontab();
  if (!current) return std::unexpected(current.error());
  auto id = make_id("job").substr(4, 8);
  auto logdir = paths::data_dir() / "schedule";
  std::error_code ec;
  fs::create_directories(logdir, ec);
  auto self = process::which("shaman").value_or("shaman");
  auto line = std::format("{} cd {} && {} run {} {} >> {} 2>&1 # shaman:{}\n", cron, sh_quote(root.string()), sh_quote(self.string()),
                          extra, sh_quote(prompt), sh_quote((logdir / (id + ".log")).string()), id);
  auto content = *current;
  if (!content.empty() && !content.ends_with("\n")) content += "\n";
  if (auto r = write_crontab(content + line); !r) return std::unexpected(r.error());
  return id;
}

Result<std::string> schedule_list() {
  auto current = read_crontab();
  if (!current) return std::unexpected(current.error());
  std::string out;
  for (auto& l : str::lines(*current))
    if (auto pos = l.find("# shaman:"); pos != std::string::npos) out += l.substr(pos + 9) + "  " + l.substr(0, pos) + "\n";
  return out.empty() ? "no scheduled runs" : out;
}

Result<void> schedule_remove(const std::string& id) {
  auto current = read_crontab();
  if (!current) return std::unexpected(current.error());
  std::string out;
  bool found = false;
  for (auto& l : str::lines(*current)) {
    if (l.ends_with("# shaman:" + id)) {
      found = true;
      continue;
    }
    out += l + "\n";
  }
  if (!found) return fail("no scheduled run " + id);
  return write_crontab(out);
}

}  // namespace shaman::extras
