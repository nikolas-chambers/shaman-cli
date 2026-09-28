#include "shaman/tool/builtin/patch.hpp"

#include <format>

#include "shaman/core/strings.hpp"
#include "shaman/tool/builtin/common.hpp"

namespace shaman::tool {

Result<std::vector<FileOp>> parse_patch(const std::string& text) {
  auto ls = str::lines(text);
  size_t i = 0;
  while (i < ls.size() && str::trim(ls[i]).empty()) ++i;
  if (i == ls.size() || str::trim(ls[i]) != "*** Begin Patch") return fail("patch must start with '*** Begin Patch'");
  ++i;
  std::vector<FileOp> ops;
  auto starts = [](const std::string& l, std::string_view p) { return l.starts_with(p); };
  while (i < ls.size()) {
    const auto& l = ls[i];
    if (str::trim(l) == "*** End Patch") return ops;
    if (starts(l, "*** Add File: ")) {
      FileOp op{FileOp::add, str::trim(l.substr(14))};
      for (++i; i < ls.size() && !starts(ls[i], "*** "); ++i) {
        if (!ls[i].starts_with("+")) return fail(std::format("line {}: added file lines must start with '+'", i + 1));
        op.content += ls[i].substr(1) + "\n";
      }
      ops.push_back(std::move(op));
    } else if (starts(l, "*** Delete File: ")) {
      ops.push_back({FileOp::remove, str::trim(l.substr(17))});
      ++i;
    } else if (starts(l, "*** Update File: ")) {
      FileOp op{FileOp::update, str::trim(l.substr(17))};
      ++i;
      if (i < ls.size() && starts(ls[i], "*** Move to: ")) op.move_to = str::trim(ls[i++].substr(13));
      Hunk h;
      bool open = false;
      auto flush = [&] {
        if (open && (!h.before.empty() || !h.after.empty())) op.hunks.push_back(h);
        h = Hunk{};
        open = false;
      };
      for (; i < ls.size() && !starts(ls[i], "*** "); ++i) {
        const auto& hl = ls[i];
        if (hl.starts_with("@@")) {
          flush();
          h.anchor = str::trim(hl.substr(2));
          open = true;
        } else if (hl.starts_with("+")) {
          h.after.push_back(hl.substr(1)), open = true;
        } else if (hl.starts_with("-")) {
          h.before.push_back(hl.substr(1)), open = true;
        } else if (hl.starts_with(" ") || hl.empty()) {
          auto body = hl.empty() ? "" : hl.substr(1);
          h.before.push_back(body), h.after.push_back(body), open = true;
        } else {
          return fail(std::format("line {}: unexpected '{}'", i + 1, hl));
        }
      }
      flush();
      if (op.hunks.empty() && op.move_to.empty()) return fail("update of " + op.path + " has no changes");
      ops.push_back(std::move(op));
    } else if (str::trim(l).empty()) {
      ++i;
    } else {
      return fail(std::format("line {}: expected a file operation, got '{}'", i + 1, l));
    }
  }
  return fail("patch must end with '*** End Patch'");
}

static std::optional<size_t> find_block(const std::vector<std::string>& hay, const std::vector<std::string>& needle,
                                        size_t from, bool loose) {
  if (needle.empty()) return from;
  for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (size_t k = 0; k < needle.size() && ok; ++k)
      ok = loose ? str::trim(hay[i + k]) == str::trim(needle[k]) : hay[i + k] == needle[k];
    if (ok) return i;
  }
  return std::nullopt;
}

Result<std::string> apply_hunks(const std::string& content, const std::vector<Hunk>& hunks) {
  bool trailing_newline = content.ends_with("\n");
  auto ls = str::lines(content);
  size_t cursor = 0;
  for (auto& h : hunks) {
    size_t from = cursor;
    if (!h.anchor.empty()) {
      auto a = find_block(ls, {h.anchor}, cursor, false);
      if (!a) a = find_block(ls, {h.anchor}, cursor, true);
      if (a) from = *a + (h.before.empty() || str::trim(h.before.front()) != str::trim(h.anchor) ? 1 : 0);
    }
    auto at = find_block(ls, h.before, from, false);
    if (!at) at = find_block(ls, h.before, from, true);
    if (!at) at = find_block(ls, h.before, 0, true);  // hunks out of order
    if (!at) {
      std::string first = h.before.empty() ? h.anchor : h.before.front();
      return fail("hunk not found near: " + first);
    }
    ls.erase(ls.begin() + long(*at), ls.begin() + long(*at + h.before.size()));
    ls.insert(ls.begin() + long(*at), h.after.begin(), h.after.end());
    cursor = *at + h.after.size();
  }
  auto out = str::join(ls, "\n");
  if (trailing_newline || content.empty()) out += "\n";
  return out;
}

namespace detail {
namespace {

class ApplyPatch final : public Tool {
 public:
  std::string name() const override { return "apply_patch"; }
  std::string description() const override {
    return "Apply a multi-file patch. Format:\n*** Begin Patch\n*** Add File: <path>\n+<line>\n*** Update File: <path>\n"
           "*** Move to: <new path> (optional)\n@@ <optional anchor line>\n <context>\n-<removed>\n+<added>\n"
           "*** Delete File: <path>\n*** End Patch\nInclude ~3 lines of context around each change. Files to "
           "update or delete must have been read first.";
  }
  Json schema() const override {
    return {{"type", "object"}, {"properties", {{"patchText", {{"type", "string"}}}}}, {"required", {"patchText"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto ops = parse_patch(in.value("patchText", ""));
    if (!ops) return error(ops.error().message);

    // Validate everything and compute results before touching the disk, so a
    // bad hunk never leaves a half-applied patch.
    struct Planned { fs::path path, dest; std::optional<std::string> content; };
    std::vector<Planned> plan;
    std::vector<std::string> summary;
    for (auto& op : *ops) {
      auto p = ctx.path(op.path);
      if (!p) return error(p.error().message);
      std::error_code ec;
      if (op.kind == FileOp::add) {
        if (fs::exists(*p, ec)) return error("cannot add " + op.path + ": file exists");
        plan.push_back({*p, *p, op.content});
        summary.push_back("A " + rel(ctx, *p));
        continue;
      }
      if (ctx.read_files && !ctx.read_files->contains(*p)) return error("read " + op.path + " before changing it");
      if (op.kind == FileOp::remove) {
        plan.push_back({*p, *p, std::nullopt});
        summary.push_back("D " + rel(ctx, *p));
        continue;
      }
      auto content = read_all(*p);
      if (!content) return error("file not found: " + op.path);
      auto updated = apply_hunks(*content, op.hunks);
      if (!updated) return error(op.path + ": " + updated.error().message);
      fs::path dest = *p;
      if (!op.move_to.empty()) {
        auto d = ctx.path(op.move_to);
        if (!d) return error(d.error().message);
        dest = *d;
      }
      plan.push_back({*p, dest, *updated});
      summary.push_back((dest == *p ? "M " : "R ") + rel(ctx, dest));
    }
    if (!ctx.permit("edit", str::join(summary, ", "), "Patch: " + str::join(summary, ", ")))
      return error("permission denied");

    std::string diagnostics;
    for (auto& step : plan) {
      std::error_code ec;
      if (!step.content) {
        fs::remove(step.path, ec);
        continue;
      }
      if (!write_all(step.dest, *step.content)) return error("failed to write " + step.dest.string());
      if (step.dest != step.path) fs::remove(step.path, ec);
      if (ctx.read_files) ctx.read_files->insert(step.dest);
      if (ctx.diagnostics) diagnostics += ctx.diagnostics(step.dest);
    }
    return {"Applied patch:\n" + str::join(summary, "\n") + diagnostics, false,
            std::format("Patch ({} files)", summary.size())};
  }
};

}  // namespace

std::unique_ptr<Tool> make_apply_patch() { return std::make_unique<ApplyPatch>(); }

}  // namespace detail
}  // namespace shaman::tool
