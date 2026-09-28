#include "shaman/extras/notify.hpp"

#include <cstdio>
#include <thread>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::extras {

NotifySettings notify_settings(const Config& config) {
  NotifySettings s;
  auto j = config.raw.value("notify", Json(true));
  if (j.is_boolean()) {
    s.enabled = j.get<bool>();
    return s;
  }
  if (!j.is_object()) return s;
  s.desktop = j.value("desktop", s.desktop);
  s.sound = j.value("sound", s.sound);
  s.min_seconds = j.value("min_seconds", s.min_seconds);
  return s;
}

void notify(const NotifySettings& s, const std::string& title, const std::string& body) {
  if (!s.enabled) return;
  if (s.sound) {
    std::fputs("\a", stderr);
    std::fflush(stderr);
  }
  if (!s.desktop) return;
  auto q = [](const std::string& v) { return "'" + str::replace_all(v, "'", "'\\''") + "'"; };
  std::string cmd;
#if defined(__APPLE__)
  cmd = "osascript -e " + q("display notification \"" + str::replace_all(body, "\"", "'") + "\" with title \"" +
                            str::replace_all(title, "\"", "'") + "\"");
#elif defined(_WIN32)
  cmd = "powershell -NoProfile -Command \"[reflection.assembly]::loadwithpartialname('System.Windows.Forms') | Out-Null; "
        "$n = New-Object System.Windows.Forms.NotifyIcon; $n.Icon = [System.Drawing.SystemIcons]::Information; $n.Visible = $true; "
        "$n.ShowBalloonTip(5000, '" + str::replace_all(title, "'", "") + "', '" + str::replace_all(body, "'", "") + "', 'Info')\"";
#else
  if (process::termux()) {  // needs the Termux:API app and `pkg install termux-api`
    if (!process::which("termux-notification")) return;
    cmd = "termux-notification --group shaman -t " + q(title) + " -c " + q(body);
  } else {
    if (!process::which("notify-send")) return;
    cmd = "notify-send -a shaman " + q(title) + " " + q(body);
  }
#endif
  std::thread([cmd] { process::shell(cmd, {.timeout = std::chrono::seconds(10)}); }).detach();
}

}  // namespace shaman::extras
