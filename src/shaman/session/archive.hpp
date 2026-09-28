#pragma once

#include <map>
#include <string>

#include "shaman/session/store.hpp"

// Getting sessions out of (and into) shaman.
namespace shaman::session {

Json export_json(const Info& info, const std::vector<llm::Message>& messages);
std::string export_markdown(const Info& info, const std::vector<llm::Message>& messages);
std::string export_html(const Info& info, const std::vector<llm::Message>& messages);  // self-contained page
Result<Info> import_json(Store& store, const Json& archive);

struct Stats {
  int sessions = 0, turns = 0, messages = 0, tool_calls = 0, tool_errors = 0;
  int64_t input_tokens = 0, output_tokens = 0, cache_read = 0;
  double cost = 0;
  std::map<std::string, int> tools, models, agents;
  std::map<std::string, int> days;  // YYYY-MM-DD -> sessions started
};

// Aggregate over sessions updated in the last `days` days (0 = all time).
Stats compute_stats(const Store& store, int days);

}  // namespace shaman::session
