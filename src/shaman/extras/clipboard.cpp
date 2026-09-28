#include "shaman/extras/clipboard.hpp"

#include <cstdlib>

#include "shaman/core/id.hpp"
#include "shaman/core/process.hpp"

namespace shaman::extras {

namespace fs = std::filesystem;

Result<fs::path> paste_image(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  auto out = dir / ("clipboard-" + make_id("img") + ".png");
  auto q = [](const fs::path& p) { return "'" + p.string() + "'"; };
  std::string cmd;
#if defined(_WIN32)
  cmd = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
        "$i = [Windows.Forms.Clipboard]::GetImage(); if ($i) { $i.Save('" + out.string() +
        "', [System.Drawing.Imaging.ImageFormat]::Png) } else { exit 1 }\"";
#elif defined(__APPLE__)
  cmd = "osascript -e 'set f to open for access POSIX file \"" + out.string() + "\" with write permission' "
        "-e 'write (the clipboard as «class PNGf») to f' -e 'close access f'";
#else
  if (process::termux()) return fail("images can't be pasted from the Android clipboard; attach a file with @path instead");
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if (wayland && *wayland && process::which("wl-paste")) cmd = "wl-paste --type image/png > " + q(out);
  else if (process::which("xclip")) cmd = "xclip -selection clipboard -t image/png -o > " + q(out);
  else return fail("install wl-clipboard (Wayland) or xclip (X11) to paste images");
#endif
  auto r = process::shell(cmd, {.timeout = std::chrono::seconds(10)});
  if (!r || r->exit_code != 0 || !fs::exists(out, ec) || fs::file_size(out, ec) < 8) {
    fs::remove(out, ec);
    return fail("no image on the clipboard");
  }
  return out;
}

Result<std::string> copy_text(const std::string& text) {
  std::string cmd;
#if defined(_WIN32)
  cmd = "clip";
#elif defined(__APPLE__)
  cmd = "pbcopy";
#else
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if (process::termux() && process::which("termux-clipboard-set")) cmd = "termux-clipboard-set";
  else if (wayland && *wayland && process::which("wl-copy")) cmd = "wl-copy";
  else if (process::which("xclip")) cmd = "xclip -selection clipboard";
  else if (process::which("xsel")) cmd = "xsel --clipboard --input";
  else return fail("no clipboard tool (wl-copy, xclip, xsel)");
#endif
  process::Options o{.timeout = std::chrono::seconds(5)};
  o.input = text;
  auto r = process::shell(cmd, o);
  if (!r || r->exit_code != 0) return fail(cmd + " failed");
  return cmd;
}

}  // namespace shaman::extras
