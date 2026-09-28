// websearch, skill
#include <cstdlib>
#include <format>
#include <regex>

#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"
#include "shaman/skill/skill.hpp"
#include "shaman/tool/builtin/common.hpp"

namespace shaman::tool::detail {
namespace {

struct Hit {
  std::string title, url, snippet;
};

std::string strip_tags(const std::string& s) {
  static const std::regex tags("<[^>]+>");
  return str::trim(str::html_unescape(std::regex_replace(s, tags, "")));
}

// Brave Search when BRAVE_SEARCH_API_KEY is set, otherwise DuckDuckGo's HTML
// endpoint, which needs no key.
Result<std::vector<Hit>> search(const std::string& query, int limit, std::atomic<bool>* cancel) {
  std::vector<Hit> hits;
  http::Request req;
  req.timeout_s = 20;
  req.cancel = cancel;
  if (const char* brave = std::getenv("BRAVE_SEARCH_API_KEY"); brave && *brave) {
    req.url = "https://api.search.brave.com/res/v1/web/search?count=" + std::to_string(limit) + "&q=" + str::url_encode(query);
    req.headers = {{"Accept", "application/json"}, {"X-Subscription-Token", brave}};
    auto res = http::send(req);
    if (!res) return std::unexpected(res.error());
    if (res->status != 200) return fail(std::format("Brave search HTTP {}", res->status));
    try {
      auto payload = Json::parse(res->body);
      for (auto& r : payload["web"]["results"])
        hits.push_back({r.value("title", ""), r.value("url", ""), strip_tags(r.value("description", ""))});
    } catch (const Json::exception& e) {
      return fail(std::string("bad search response: ") + e.what());
    }
    return hits;
  }
  const char* base = std::getenv("SHAMAN_SEARCH_URL");  // test hook
  req.url = std::string(base && *base ? base : "https://html.duckduckgo.com/html/") + "?q=" + str::url_encode(query);
  req.headers = {{"User-Agent", "Mozilla/5.0 (compatible; shaman-cli)"}};
  auto res = http::send(req);
  if (!res) return std::unexpected(res.error());
  if (res->status != 200) return fail(std::format("search HTTP {}", res->status));
  static const std::regex link(R"re(<a[^>]*class="result__a"[^>]*href="([^"]+)"[^>]*>([\s\S]*?)</a>)re");
  static const std::regex snippet(R"re(<a[^>]*class="result__snippet"[^>]*>([\s\S]*?)</a>)re");
  std::vector<std::string> snippets;
  for (auto it = std::sregex_iterator(res->body.begin(), res->body.end(), snippet); it != std::sregex_iterator(); ++it)
    snippets.push_back(strip_tags((*it)[1]));
  size_t n = 0;
  for (auto it = std::sregex_iterator(res->body.begin(), res->body.end(), link);
       it != std::sregex_iterator() && int(hits.size()) < limit; ++it, ++n) {
    std::string url = str::html_unescape((*it)[1]);
    if (auto u = url.find("uddg="); u != std::string::npos) url = str::url_decode(url.substr(u + 5, url.find('&', u) - u - 5));
    if (url.starts_with("//")) url = "https:" + url;
    hits.push_back({strip_tags((*it)[2]), url, n < snippets.size() ? snippets[n] : ""});
  }
  return hits;
}

class WebSearch final : public Tool {
 public:
  std::string name() const override { return "websearch"; }
  std::string description() const override {
    return "Search the web. Returns titles, URLs and snippets; follow up with webfetch for full pages. "
           "Use for current information, docs and error messages.";
  }
  Json schema() const override {
    return {{"type", "object"},
            {"properties", {{"query", {{"type", "string"}}}, {"limit", {{"type", "integer"}, {"description", "1-10, default 8"}}}}},
            {"required", {"query"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto query = in.value("query", "");
    if (query.empty()) return error("query is required");
    if (!ctx.permit("websearch", query, "Search: " + query)) return error("permission denied");
    auto hits = search(query, std::clamp<int>(in.value("limit", 8), 1, 10), ctx.cancel);
    if (!hits) return error(hits.error().message);
    if (hits->empty()) return {"No results", false, "Search: " + query};
    std::string out;
    int i = 0;
    for (auto& h : *hits) out += std::format("{}. {}\n   {}\n   {}\n", ++i, h.title, h.url, h.snippet);
    return {out, false, std::format("Search: {} ({} results)", query, hits->size())};
  }
};

class SkillTool final : public Tool {
 public:
  explicit SkillTool(const fs::path& root) : skills_(skill::discover(root)) {}
  std::string name() const override { return "skill"; }
  std::string description() const override {
    std::string d = "Load a skill: specialised instructions for a kind of task. Load one when the task matches its "
                    "description, then follow it.";
    if (skills_.empty()) return d + " (No skills are installed.)";
    d += "\nAvailable skills:";
    for (auto& s : skills_) d += "\n- " + s.name + ": " + s.description;
    return d;
  }
  Json schema() const override {
    return {{"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
  }
  Output run(const Json& in, Context& ctx) override {
    auto name = in.value("name", "");
    for (auto& s : skills_) {
      if (s.name != name) continue;
      if (!ctx.permit("skill", name, "Load skill " + name)) return error("permission denied");
      return {std::format("<skill name=\"{}\" dir=\"{}\">\n{}\n</skill>\nScripts and files mentioned above live in {} "
                          "(run them with their full path).",
                          s.name, s.dir.string(), s.body, s.dir.string()),
              false, "Skill " + name};
    }
    return error("no such skill: " + name);
  }

 private:
  const std::vector<skill::Skill>& skills_;
};

}  // namespace

std::unique_ptr<Tool> make_websearch() { return std::make_unique<WebSearch>(); }
std::unique_ptr<Tool> make_skill(const fs::path& root) { return std::make_unique<SkillTool>(root); }

}  // namespace shaman::tool::detail
