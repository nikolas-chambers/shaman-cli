#include "shaman/tool/builtin/common.hpp"

namespace shaman::tool::detail {
namespace {

class Lsp final : public Tool {
 public:
  std::string name() const override { return "lsp"; }
  std::string description() const override {
    return "Ask the language server about code. operation: diagnostics (errors in a file), hover (type/docs at a "
           "position), definition, references, symbols (outline of a file). line and column are 1-based.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"operation", {{"type", "string"}, {"enum", {"diagnostics", "hover", "definition", "references", "symbols"}}}},
                            {"filePath", {{"type", "string"}}}, {"line", {{"type", "integer"}}}, {"column", {{"type", "integer"}}}}},
            {"required", {"operation", "filePath"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    if (!ctx.lsp) return error("no language servers available");
    auto p = ctx.path(in.value("filePath", ""));
    if (!p) return error(p.error().message);
    auto op = in.value("operation", "diagnostics");
    auto r = ctx.lsp(op, *p, in.value("line", 1), in.value("column", 1));
    if (!r) return error(r.error().message);
    return {truncate(*r), false, "LSP " + op + " " + rel(ctx, *p)};
  }
};

}  // namespace

std::unique_ptr<Tool> make_lsp() { return std::make_unique<Lsp>(); }

}  // namespace shaman::tool::detail
