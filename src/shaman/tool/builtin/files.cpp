// read, write, edit
#include <format>

#include "shaman/core/strings.hpp"
#include "shaman/tool/builtin/common.hpp"
#include "shaman/tool/builtin/edit.hpp"

namespace shaman::tool {

Result<std::string> apply_edit(const std::string& content, const std::string& old_text,
                               const std::string& new_text, bool replace_all) {
  if (old_text == new_text) return fail("oldString and newString are identical");
  size_t n = str::count(content, old_text);
  if (n == 1 || (n > 1 && replace_all)) {
    if (replace_all) return str::replace_all(content, old_text, new_text);
    auto pos = content.find(old_text);
    return content.substr(0, pos) + new_text + content.substr(pos + old_text.size());
  }
  if (n > 1)
    return fail(std::format("oldString matches {} places; add surrounding context or set replaceAll", n));

  // Whitespace-tolerant line match.
  auto hay = str::split(content, '\n');
  auto needle = str::lines(old_text);
  if (needle.empty()) return fail("oldString not found");
  std::vector<size_t> hits;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (size_t k = 0; k < needle.size() && ok; ++k) ok = str::trim(hay[i + k]) == str::trim(needle[k]);
    if (ok) hits.push_back(i);
  }
  if (hits.empty()) return fail("oldString not found in file");
  if (hits.size() > 1 && !replace_all)
    return fail(std::format("oldString matches {} places (ignoring whitespace); add more context", hits.size()));
  auto replacement = str::split(new_text, '\n');
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    hay.erase(hay.begin() + long(*it), hay.begin() + long(*it + needle.size()));
    hay.insert(hay.begin() + long(*it), replacement.begin(), replacement.end());
  }
  return str::join(hay, "\n");
}

namespace detail {
namespace {

class Read final : public Tool {
 public:
  std::string name() const override { return "read"; }
  std::string description() const override {
    return "Read a file. Returns numbered lines. Use offset (1-based line) and limit to page through "
           "large files. Read a file before editing it.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"filePath", {{"type", "string"}, {"description", "Path to the file"}}},
                            {"offset", {{"type", "integer"}, {"description", "First line, 1-based"}}},
                            {"limit", {{"type", "integer"}, {"description", "Max lines (default 2000)"}}}}},
            {"required", {"filePath"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto p = ctx.path(in.value("filePath", ""));
    if (!p) return error(p.error().message);
    if (!ctx.permit("read", p->string(), "Read " + rel(ctx, *p))) return error("permission denied");
    std::error_code ec;
    if (fs::is_directory(*p, ec)) return error("that is a directory; use the list tool");
    auto content = read_all(*p);
    if (!content) return error("file not found: " + p->string());
    if (content->substr(0, 8192).find('\0') != std::string::npos) return error("binary file; not shown");
    if (ctx.read_files) ctx.read_files->insert(*p);

    auto lines = str::lines(*content);
    size_t offset = std::max<int64_t>(in.value("offset", 1), 1) - 1;
    size_t limit = std::max<int64_t>(in.value("limit", 2000), 1);
    std::string out;
    for (size_t i = offset; i < lines.size() && i < offset + limit; ++i) {
      auto line = lines[i].size() > 2000 ? lines[i].substr(0, 2000) + "..." : lines[i];
      out += std::format("{:>6}\t{}\n", i + 1, line);
    }
    if (offset + limit < lines.size())
      out += std::format("\n({} more lines; continue with offset={})", lines.size() - offset - limit, offset + limit + 1);
    return {truncate(out), false, "Read " + rel(ctx, *p)};
  }
};

class Write final : public Tool {
 public:
  std::string name() const override { return "write"; }
  std::string description() const override {
    return "Create or overwrite a file with the given content. Existing files must be read first. "
           "Prefer edit for changes to existing files.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"filePath", {{"type", "string"}}}, {"content", {{"type", "string"}}}}},
            {"required", {"filePath", "content"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto p = ctx.path(in.value("filePath", ""));
    if (!p) return error(p.error().message);
    std::error_code ec;
    bool exists = fs::exists(*p, ec);
    if (exists && ctx.read_files && !ctx.read_files->contains(*p))
      return error("read the file before overwriting it");
    if (!ctx.permit("edit", p->string(), (exists ? "Overwrite " : "Create ") + rel(ctx, *p)))
      return error("permission denied");
    if (!write_all(*p, in.value("content", ""))) return error("failed to write " + p->string());
    if (ctx.read_files) ctx.read_files->insert(*p);
    auto diag = ctx.diagnostics ? ctx.diagnostics(*p) : "";
    return {std::format("Wrote {} bytes to {}{}", in.value("content", "").size(), rel(ctx, *p), diag), false,
            (exists ? "Write " : "Create ") + rel(ctx, *p)};
  }
};

class Edit final : public Tool {
 public:
  std::string name() const override { return "edit"; }
  std::string description() const override {
    return "Replace text in a file. oldString must match exactly one place unless replaceAll is true; "
           "include enough surrounding lines to make it unique. Empty oldString creates a new file.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"filePath", {{"type", "string"}}},
                            {"oldString", {{"type", "string"}}},
                            {"newString", {{"type", "string"}}},
                            {"replaceAll", {{"type", "boolean"}}}}},
            {"required", {"filePath", "oldString", "newString"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto p = ctx.path(in.value("filePath", ""));
    if (!p) return error(p.error().message);
    auto old_text = in.value("oldString", ""), new_text = in.value("newString", "");
    std::error_code ec;
    if (old_text.empty()) {
      if (fs::exists(*p, ec)) return error("file exists; provide oldString to edit it");
      if (!ctx.permit("edit", p->string(), "Create " + rel(ctx, *p))) return error("permission denied");
      write_all(*p, new_text);
      if (ctx.read_files) ctx.read_files->insert(*p);
      return {"Created " + rel(ctx, *p), false, "Create " + rel(ctx, *p)};
    }
    if (ctx.read_files && !ctx.read_files->contains(*p)) return error("read the file before editing it");
    auto content = read_all(*p);
    if (!content) return error("file not found: " + p->string());
    auto updated = apply_edit(*content, old_text, new_text, in.value("replaceAll", false));
    if (!updated) return error(updated.error().message);
    if (!ctx.permit("edit", p->string(), "Edit " + rel(ctx, *p))) return error("permission denied");
    if (!write_all(*p, *updated)) return error("failed to write " + p->string());
    auto diag = ctx.diagnostics ? ctx.diagnostics(*p) : "";
    return {"Edited " + rel(ctx, *p) + diag, false, "Edit " + rel(ctx, *p)};
  }
};

}  // namespace

std::unique_ptr<Tool> make_read() { return std::make_unique<Read>(); }
std::unique_ptr<Tool> make_write() { return std::make_unique<Write>(); }
std::unique_ptr<Tool> make_edit() { return std::make_unique<Edit>(); }

}  // namespace detail
}  // namespace shaman::tool
