#include "shaman/tool/tool.hpp"

#include "shaman/core/paths.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::tool {

bool Context::permit(std::string permission, std::string subject, std::string title) const {
  if (!gate) return true;
  return gate->check({std::move(permission), std::move(subject), std::move(title)});
}

Result<fs::path> Context::path(const std::string& p) const {
  if (p.empty()) return fail("path is required");
  auto resolved = paths::resolve(root, p);
  if (!paths::within(root, resolved) && resolved != root &&
      !permit("external_directory", resolved.string(), "Access outside project: " + resolved.string()))
    return fail("access outside the project was denied: " + resolved.string());
  return resolved;
}

void Registry::add(std::unique_ptr<Tool> tool) {
  auto name = tool->name();
  tools_[name] = std::move(tool);
}

Tool* Registry::find(const std::string& name) const {
  auto it = tools_.find(name);
  return it == tools_.end() ? nullptr : it->second.get();
}

std::vector<Tool*> Registry::all() const {
  std::vector<Tool*> out;
  for (auto& [_, t] : tools_) out.push_back(t.get());
  return out;
}

std::string truncate(std::string text, size_t max_lines, size_t max_bytes) {
  bool cut = false;
  if (text.size() > max_bytes) {
    text.resize(max_bytes);
    cut = true;
  }
  size_t lines = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n' && ++lines >= max_lines) {
      text.resize(i);
      cut = true;
      break;
    }
  }
  if (cut) text += "\n\n[output truncated; narrow the query or use offset/limit]";
  return text;
}

}  // namespace shaman::tool
