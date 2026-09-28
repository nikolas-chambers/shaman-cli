#include "shaman/config/config.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"

namespace shaman {
namespace {

std::optional<std::string> read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string strip_trailing_commas(std::string_view text) {
  std::string out;
  bool in_str = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (in_str) {
      out += c;
      if (c == '\\' && i + 1 < text.size()) out += text[++i];
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') in_str = true;
    if (c == ',') {
      size_t j = i + 1;
      while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
      if (j < text.size() && (text[j] == '}' || text[j] == ']')) continue;
    }
    out += c;
  }
  return out;
}

Result<Json> load_layer(const fs::path& p) {
  auto text = read_file(p);
  if (!text) return fail("cannot read " + p.string());
  auto json = parse_jsonc(*text);
  if (!json) return fail(p.string() + ": " + json.error().message);
  substitute(*json, p.parent_path());
  return json;
}

}  // namespace

Result<Json> parse_jsonc(std::string_view text) {
  // nlohmann handles comments natively; trailing commas we strip ourselves.
  // Comments are stripped by the parser, so a comma before a comment is not
  // a concern, but a comment between a comma and '}' would defeat the strip.
  try {
    return Json::parse(strip_trailing_commas(text), nullptr, true, true);
  } catch (const Json::parse_error& e) {
    return fail(e.what());
  }
}

void substitute(Json& value, const fs::path& base) {
  if (value.is_object() || value.is_array()) {
    for (auto& child : value) substitute(child, base);
    return;
  }
  if (!value.is_string()) return;
  static const std::regex re(R"(\{(env|file):([^}]+)\})");
  std::string s = value.get<std::string>(), out;
  auto begin = std::sregex_iterator(s.begin(), s.end(), re);
  size_t last = 0;
  for (auto it = begin; it != std::sregex_iterator(); ++it) {
    out += s.substr(last, it->position() - last);
    std::string kind = (*it)[1], arg = (*it)[2];
    if (kind == "env") {
      const char* v = std::getenv(arg.c_str());
      out += v ? v : "";
    } else {
      auto p = arg.starts_with("~/") ? fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / arg.substr(2)
                                     : paths::resolve(base, arg);
      auto content = read_file(p);
      out += content ? *content : "";
      if (!content) log::warn("config: {file:" + arg + "} not found");
    }
    last = it->position() + it->length();
  }
  if (last == 0) return;
  out += s.substr(last);
  value = out;
}

Result<Config> Config::load(const fs::path& cwd, const fs::path& project_root) {
  Json merged = Json::object();
  std::vector<fs::path> sources;
  auto apply = [&](const fs::path& p) -> Result<void> {
    auto layer = load_layer(p);
    if (!layer) return std::unexpected(layer.error());
    log::debug(log::Cat::config, "layer {}: {}", sources.size() + 1, p.string());
    merged.merge_patch(*layer);
    sources.push_back(p);
    return {};
  };
  auto try_names = [&](const fs::path& dir) -> Result<void> {
    for (auto name : {"shaman.json", "shaman.jsonc", ".shaman/shaman.json", ".shaman/shaman.jsonc"}) {
      std::error_code ec;
      if (fs::is_regular_file(dir / name, ec))
        if (auto r = apply(dir / name); !r) return r;
    }
    return {};
  };

  if (auto r = try_names(paths::config_dir()); !r) return std::unexpected(r.error());

  // Walk from the project root down to cwd so nearer files win.
  std::vector<fs::path> chain;
  for (fs::path d = cwd; paths::within(project_root, d) || d == project_root; d = d.parent_path()) {
    chain.push_back(d);
    if (d == project_root || d == d.root_path()) break;
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    if (auto r = try_names(*it); !r) return std::unexpected(r.error());

  if (const char* p = std::getenv("SHAMAN_CONFIG"); p && *p)
    if (auto r = apply(p); !r) return std::unexpected(r.error());
  if (const char* c = std::getenv("SHAMAN_CONFIG_CONTENT"); c && *c) {
    auto layer = parse_jsonc(c);
    if (!layer) return fail("SHAMAN_CONFIG_CONTENT: " + layer.error().message);
    substitute(*layer, cwd);
    merged.merge_patch(*layer);
    log::debug(log::Cat::config, "layer: $SHAMAN_CONFIG_CONTENT");
  }

  auto cfg = from_json(merged);
  if (cfg) cfg->sources = std::move(sources);
  return cfg;
}

Result<Config> Config::from_json(const Json& j) {
  Config c;
  c.raw = j;
  try {
    c.model = j.value("model", "");
    c.default_agent = j.value("default_agent", c.default_agent);
    c.free_fallback = j.value("free_fallback", c.free_fallback);
    c.snapshot = j.value("snapshot", c.snapshot);
    c.permission = j.value("permission", Json::object());
    c.agent = j.value("agent", Json::object());
    c.provider = j.value("provider", Json::object());
    c.instructions = j.value("instructions", std::vector<std::string>{});
    auto mcp = j.value("mcp", Json::object());
    for (auto& [name, m] : mcp.items()) {
      McpServerConfig s;
      s.url = m.value("url", "");
      s.type = m.value("type", s.url.empty() ? "local" : "remote");
      s.command = m.value("command", std::vector<std::string>{});
      s.environment = m.value("environment", std::map<std::string, std::string>{});
      s.headers = m.value("headers", std::map<std::string, std::string>{});
      s.oauth = m.value("oauth", Json(true));
      s.enabled = m.value("enabled", true);
      s.timeout_ms = m.value("timeout", s.timeout_ms);
      c.mcp[name] = std::move(s);
    }
  } catch (const Json::exception& e) {
    return fail(std::string("invalid config: ") + e.what());
  }
  return c;
}

}  // namespace shaman
