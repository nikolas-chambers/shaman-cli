// shaman-desktop: the web UI in a native window (WebView2 on Windows,
// WKWebView on macOS, WebKitGTK on Linux), backed by an in-process server.
//
//   shaman-desktop [project-dir]
//
// The server binds 127.0.0.1 on a random port and requires a random token,
// which is handed to the page directly, so nothing else on the machine can
// drive it.
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

#include "shaman/cli/app.hpp"
#include "shaman/core/log.hpp"
#include "shaman/mcp/oauth.hpp"
#include "shaman/server/server.hpp"
#include "webview/webview.h"

namespace {

int run(int argc, char** argv) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::current_path(ec);
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
