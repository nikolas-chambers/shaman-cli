#include "shaman/session/archive.hpp"

#include <chrono>
#include <format>

#include "shaman/core/id.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/version.hpp"

namespace shaman::session {

using namespace llm;

Json export_json(const Info& info, const std::vector<Message>& messages) {
  Json ms = Json::array();
  for (auto& m : messages) ms.push_back(to_json(m));
  return {{"format", "shaman-session"}, {"version", 1}, {"shaman", kVersion}, {"info", to_json(info)}, {"messages", ms}};
}

static std::string date(int64_t ms) {
  auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
  return std::format("{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::minutes>(tp));
}

std::string export_markdown(const Info& info, const std::vector<Message>& messages) {
  std::string out = std::format("# {}\n\n`{}` · agent {} · model {} · {}\n\n", info.title.empty() ? info.id : info.title,
                                info.id, info.agent, info.model, date(info.created));
  for (auto& m : messages) {
    bool results = !m.parts.empty() && std::holds_alternative<ToolResultPart>(m.parts.front());
    if (!results) out += m.role == Role::user ? "## User\n\n" : "## Assistant\n\n";
    for (auto& p : m.parts) {
      if (auto* t = std::get_if<TextPart>(&p)) out += t->text + "\n\n";
      else if (std::holds_alternative<ImagePart>(p)) out += "*[image]*\n\n";
      else if (auto* c = std::get_if<ToolCallPart>(&p)) out += std::format("**Tool:** `{}` `{}`\n\n", c->name, c->input.dump());
      else if (auto* r = std::get_if<ToolResultPart>(&p)) {
        auto text = r->output.size() > 2000 ? r->output.substr(0, 2000) + "\n..." : r->output;
        out += std::format("<details><summary>{} {}</summary>\n\n```\n{}\n```\n</details>\n\n", r->is_error ? "error" : "result", r->name, text);
      }
    }
  }
  out += std::format("---\n{} input / {} output tokens · ${:.4f}\n", info.usage.input, info.usage.output, info.cost);
  return out;
}

static std::string esc(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string export_html(const Info& info, const std::vector<Message>& messages) {
  std::string body;
  for (auto& m : messages) {
    bool results = !m.parts.empty() && std::holds_alternative<ToolResultPart>(m.parts.front());
    std::string cls = results ? "tool" : m.role == Role::user ? "user" : "assistant";
    body += "<section class=\"" + cls + "\">";
    if (!results) body += std::format("<h2>{}</h2>", m.role == Role::user ? "You" : "Shaman");
    for (auto& p : m.parts) {
      if (auto* t = std::get_if<TextPart>(&p)) body += "<pre class=\"text\">" + esc(t->text) + "</pre>";
      else if (auto* i = std::get_if<ImagePart>(&p)) body += "<img src=\"data:" + i->media_type + ";base64," + i->data + "\">";
      else if (auto* c = std::get_if<ToolCallPart>(&p)) body += "<div class=\"call\">&#9656; " + esc(c->name) + " <code>" + esc(c->input.dump()) + "</code></div>";
      else if (auto* r = std::get_if<ToolResultPart>(&p))
        body += std::format("<details><summary>{}{}</summary><pre>{}</pre></details>", esc(r->name), r->is_error ? " (error)" : "",
                            esc(r->output.size() > 20000 ? r->output.substr(0, 20000) + "\n..." : r->output));
    }
    body += "</section>\n";
  }
  return std::format(R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{}</title><style>
:root{{--bg:#fff;--fg:#1d1d1f;--muted:#6e6e73;--card:#f5f5f7;--accent:#5b4bdb}}
@media (prefers-color-scheme:dark){{:root{{--bg:#111114;--fg:#ececf1;--muted:#9a9aa3;--card:#1c1c21;--accent:#a99cff}}}}
body{{background:var(--bg);color:var(--fg);font:15px/1.55 system-ui,sans-serif;max-width:860px;margin:0 auto;padding:24px 16px}}
header p{{color:var(--muted)}} h1{{font-size:22px}} h2{{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:0 0 6px}}
section{{background:var(--card);border-radius:10px;padding:14px 16px;margin:12px 0}} section.user{{border-left:3px solid var(--accent)}}
section.tool{{background:transparent;padding:0 16px}} pre{{white-space:pre-wrap;word-wrap:break-word;font:13px/1.5 ui-monospace,monospace;margin:6px 0}}
pre.text{{font:inherit}} .call{{font-size:13px;color:var(--muted)}} code{{font-size:12px}} details summary{{cursor:pointer;color:var(--muted);font-size:13px}}
img{{max-width:100%;border-radius:6px}} footer{{color:var(--muted);font-size:13px;margin-top:24px}}
</style></head><body><header><h1>{}</h1><p>{} · {} · {}</p></header>
{}<footer>{} input / {} output tokens · ${:.4f} · shared from shaman {}</footer></body></html>
)",
                     esc(info.title), esc(info.title.empty() ? info.id : info.title), esc(info.agent), esc(info.model),
                     date(info.created), body, info.usage.input, info.usage.output, info.cost, kVersion);
}

Result<Info> import_json(Store& store, const Json& archive) {
  if (archive.value("format", "") != "shaman-session") return fail("not a shaman session export");
  auto src = info_from_json(archive.value("info", Json::object()));
  auto info = store.create(src.agent, src.model);
  if (!info) return info;
  info->title = src.title.empty() ? "imported" : src.title + " (imported)";
  info->usage = src.usage;
  info->cost = src.cost;
  std::vector<Message> ms;
  for (auto& m : archive.value("messages", Json::array())) ms.push_back(message_from_json(m));
  if (auto r = store.replace_messages(info->id, ms); !r) return std::unexpected(r.error());
  if (auto r = store.save(*info); !r) return std::unexpected(r.error());
  return info;
}

Stats compute_stats(const Store& store, int days) {
  Stats s;
  auto cutoff = days > 0 ? now_ms() - int64_t(days) * 86'400'000 : 0;
  for (auto& info : store.list()) {
    if (info.updated < cutoff) continue;
    ++s.sessions;
    s.input_tokens += info.usage.input;
    s.output_tokens += info.usage.output;
    s.cache_read += info.usage.cache_read;
    s.cost += info.cost;
    if (!info.model.empty()) ++s.models[info.model];
    ++s.agents[info.agent];
    ++s.days[date(info.created).substr(0, 10)];
    auto ms = store.messages(info.id);
    if (!ms) continue;
    for (auto& m : *ms) {
      ++s.messages;
      for (auto& p : m.parts) {
        if (auto* c = std::get_if<ToolCallPart>(&p)) ++s.tool_calls, ++s.tools[c->name];
        else if (auto* r = std::get_if<ToolResultPart>(&p); r && r->is_error) ++s.tool_errors;
        else if (m.role == Role::user && std::holds_alternative<TextPart>(p)) ++s.turns;
      }
    }
  }
  return s;
}

}  // namespace shaman::session
