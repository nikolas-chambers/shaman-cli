#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace shaman {

// Time-sortable id: "<prefix>_<12 hex ms><8 hex random>".
std::string make_id(std::string_view prefix);

int64_t now_ms();

}  // namespace shaman
