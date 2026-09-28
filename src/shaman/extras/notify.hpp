#pragma once

#include <string>

#include "shaman/config/config.hpp"

// Desktop notification plus terminal bell, for when you step away.
//   "notify": { "desktop": true, "sound": true, "min_seconds": 20 }   or "notify": false
namespace shaman::extras {

struct NotifySettings {
  bool enabled = true, desktop = true, sound = true;
  int min_seconds = 20;  // only notify for turns that took at least this long
};

NotifySettings notify_settings(const Config& config);
void notify(const NotifySettings& s, const std::string& title, const std::string& body);

}  // namespace shaman::extras
