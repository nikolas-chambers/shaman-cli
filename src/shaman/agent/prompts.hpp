#pragma once

#include <string_view>

namespace shaman::agent::prompts {

extern const std::string_view base;        // every primary agent
extern const std::string_view plan;        // plan agent addendum
extern const std::string_view explore;     // explore subagent
extern const std::string_view general;     // general subagent
extern const std::string_view compaction;  // summarise a long session

}  // namespace shaman::agent::prompts
