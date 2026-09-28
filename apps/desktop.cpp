// shaman-desktop: the web UI in a native window (WebView2 on Windows,
// WKWebView on macOS, WebKitGTK on Linux), backed by an in-process server.
//
//   shaman-desktop [project-dir]
//
// Without a directory (a double-click), it reopens the last project, or your home folder.
//
// The server binds 127.0.0.1 on a random port and requires a random token,
// which is handed to the page directly, so nothing else on the machine can
// drive it.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

#include "shaman/cli/app.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/mcp/oauth.hpp"
#include "shaman/server/server.hpp"
#include "webview/webview.h"

namespace {

namespace fs = std::filesystem;

// Which project to open. Launched from Finder, the Start menu or a dock, the working directory is "/" or
// the app's own folder, which is never what you want: reopen the last project, else your home folder.
fs::path project_dir(int argc, char** argv) {
  std::error_code ec;
  auto remember = shaman::paths::data_dir() / "desktop-last-project";
  if (argc > 1 && !std::string_view(argv[1]).starts_with("-psn")) {  // -psn_...: old macOS process serial number
    auto dir = fs::absolute(argv[1], ec);
    fs::create_directories(remember.parent_path(), ec);
    std::ofstream(remember) << dir.string();
    return dir;
  }
  auto cwd = fs::current_path(ec);
  auto exe_dir = shaman::paths::executable().parent_path();
  bool launched_by_os = cwd.empty() || cwd == cwd.root_path() || cwd == exe_dir ||
                        cwd.string().find(".app/Contents") != std::string::npos ||
                        cwd.string().find("System32") != std::string::npos;
  if (!launched_by_os) return cwd;
  std::ifstream in(remember);
  std::string last;
  if (std::getline(in, last) && fs::is_directory(last, ec)) return last;
  const char* home = std::getenv("HOME");
  if (!home) home = std::getenv("USERPROFILE");
  return home ? fs::path(home) : cwd;
}

int run(int argc, char** argv) {
  std::error_code ec;
#ifdef __APPLE__
  // Apps started from Finder get a bare PATH (no Homebrew, node, ...): use the one from your login shell.
  if (const char* sh = std::getenv("SHELL"); sh && *sh) {
    auto r = shaman::process::shell(std::string(sh) + " -lc 'printf %s \"$PATH\"'", {.timeout = std::chrono::seconds(5)});
    if (r && r->exit_code == 0 && !r->output.empty()) setenv("PATH", r->output.c_str(), 1);
  }
#endif
  fs::path dir = project_dir(argc, argv);
  if (const char* d = std::getenv("SHAMAN_DEBUG"); d && *d) shaman::log::enable(shaman::log::parse_categories(d));

  auto app = shaman::cli::App::create(dir, true);
  if (!app) {
    std::cerr << "shaman: " << app.error().message << "\n";
    return 1;
  }
  shaman::server::Options opts;
  opts.port = 0;
  opts.token = shaman::mcp::oauth::random_token();
  std::promise<std::string> url;
  opts.on_listen = [&](const std::string& u) { url.set_value(u); };
  std::thread([&] { shaman::server::serve(**app, opts); }).detach();
  auto address = url.get_future().get();

  webview::webview w(false, nullptr);
  w.set_title("shaman - " + (*app)->root.filename().string());
  w.set_size(1180, 800, WEBVIEW_HINT_NONE);
  w.init("try { localStorage.setItem('shaman.token', '" + opts.token + "'); } catch (e) {}");
  w.navigate(address + "/");
  w.run();
  std::_Exit(0);  // server thread is detached; nothing to flush
}

}  // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return run(__argc, __argv); }
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
