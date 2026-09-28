#include "shaman/session/store.hpp"

#include <algorithm>
#include <fstream>

#include "shaman/core/id.hpp"
#include "shaman/core/log.hpp"

namespace shaman::session {

Json to_json(const Info& i) {
  return {{"id", i.id}, {"title", i.title}, {"agent", i.agent}, {"model", i.model},
          {"parent_id", i.parent_id}, {"created", i.created}, {"updated", i.updated},
          {"usage", {{"input", i.usage.input}, {"output", i.usage.output}, {"cache_read", i.usage.cache_read}}},
          {"cost", i.cost}, {"snapshots", i.snapshots}, {"goal", i.goal}, {"turn_starts", i.turn_starts}};
}

Info info_from_json(const Json& j) {
  Info i;
  i.id = j.value("id", "");
  i.title = j.value("title", "");
  i.agent = j.value("agent", "build");
  i.model = j.value("model", "");
  i.parent_id = j.value("parent_id", "");
  i.created = j.value("created", int64_t(0));
  i.updated = j.value("updated", int64_t(0));
  auto u = j.value("usage", Json::object());
  i.usage.input = u.value("input", int64_t(0));
  i.usage.output = u.value("output", int64_t(0));
  i.usage.cache_read = u.value("cache_read", int64_t(0));
  i.cost = j.value("cost", 0.0);
  i.snapshots = j.value("snapshots", std::vector<std::string>{});
  i.goal = j.value("goal", "");
  i.turn_starts = j.value("turn_starts", std::vector<size_t>{});
  return i;
}

Result<Info> Store::create(std::string agent, std::string model, std::string parent_id) {
  Info i;
  i.id = make_id("ses");
  i.agent = std::move(agent);
  i.model = std::move(model);
  i.parent_id = std::move(parent_id);
  i.created = i.updated = now_ms();
  std::error_code ec;
  fs::create_directories(dir_ / i.id, ec);
  if (ec) return fail("cannot create session dir: " + ec.message());
  if (auto r = save(i); !r) return std::unexpected(r.error());
  log::debug(log::Cat::session, "created {} ({}, {})", i.id, i.agent, i.model);
  return i;
}

Result<Info> Store::get(const std::string& id) const {
  std::ifstream in(dir_ / id / "info.json");
  if (!in) return fail("no such session: " + id);
  try {
    return info_from_json(Json::parse(in));
  } catch (const Json::exception& e) {
    return fail("corrupt session " + id + ": " + e.what());
  }
}

Result<void> Store::save(const Info& info) const {
  auto tmp = dir_ / info.id / "info.json.tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << to_json(info).dump(2);
    if (!out) return fail("cannot write session info");
  }
  std::error_code ec;
  fs::rename(tmp, dir_ / info.id / "info.json", ec);
  if (ec) return fail("cannot save session info: " + ec.message());
  return {};
}

Result<void> Store::append(const std::string& id, const llm::Message& m) const {
  std::ofstream out(dir_ / id / "messages.jsonl", std::ios::app);
  out << llm::to_json(m).dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
  if (!out) return fail("cannot append to session " + id);
  return {};
}

Result<std::vector<llm::Message>> Store::messages(const std::string& id) const {
  std::vector<llm::Message> out;
  std::ifstream in(dir_ / id / "messages.jsonl");
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    try {
      out.push_back(llm::message_from_json(Json::parse(line)));
    } catch (const Json::exception&) {
      log::warn("session " + id + ": skipping a corrupt message line");
    }
  }
  return out;
}

Result<void> Store::replace_messages(const std::string& id, const std::vector<llm::Message>& ms) const {
  auto tmp = dir_ / id / "messages.jsonl.tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    for (auto& m : ms) out << llm::to_json(m).dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    if (!out) return fail("cannot rewrite session " + id);
  }
  std::error_code ec;
  fs::rename(tmp, dir_ / id / "messages.jsonl", ec);
  if (ec) return fail(ec.message());
  return {};
}

std::vector<Info> Store::list() const {
  std::vector<Info> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir_, ec))
    if (auto i = get(e.path().filename().string()); i && i->parent_id.empty()) out.push_back(*i);
  std::ranges::sort(out, std::greater{}, &Info::updated);
  return out;
}

std::optional<Info> Store::latest() const {
  auto all = list();
  if (all.empty()) return std::nullopt;
  return all.front();
}

}  // namespace shaman::session
