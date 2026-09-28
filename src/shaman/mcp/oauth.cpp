#include "shaman/mcp/oauth.hpp"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <format>
#include <fstream>
#include <iostream>

#include "shaman/core/id.hpp"
#include "shaman/core/log.hpp"
#include "shaman/core/net.hpp"
#include "shaman/core/paths.hpp"
#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"
#include "shaman/http/http.hpp"

namespace shaman::mcp::oauth {
namespace fs = std::filesystem;
namespace {

fs::path store_path() { return paths::data_dir() / "mcp-auth.json"; }

Json load_store() {
  std::ifstream in(store_path());
  if (!in) return Json::object();
  try {
    return Json::parse(in);
  } catch (...) {
    return Json::object();
  }
}

void save_store(const Json& j) {
  std::error_code ec;
  fs::create_directories(store_path().parent_path(), ec);
  auto tmp = store_path();
  tmp += ".tmp";
  std::ofstream(tmp, std::ios::trunc) << j.dump(2);
  fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
  fs::rename(tmp, store_path(), ec);
}

std::string base64url(std::string_view data) {
  auto s = str::base64_encode(data);
  for (auto& c : s) c = c == '+' ? '-' : c == '/' ? '_' : c;
  while (!s.empty() && s.back() == '=') s.pop_back();
  return s;
}

std::string random_token(size_t bytes = 32) {
  std::string buf(bytes, '\0');
  RAND_bytes(reinterpret_cast<unsigned char*>(buf.data()), int(bytes));
  return base64url(buf);
}

std::string origin(const std::string& url) {
  auto scheme = url.find("://");
  auto slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  return slash == std::string::npos ? url : url.substr(0, slash);
}

Result<Json> get_json(const std::string& url) {
  http::Request req;
  req.url = url;
  req.timeout_s = 20;
  req.headers = {{"Accept", "application/json"}};
  auto res = http::send(req);
  if (!res) return std::unexpected(res.error());
  if (res->status != 200) return fail(std::format("GET {} -> HTTP {}", url, res->status), int(res->status));
  try {
    return Json::parse(res->body);
  } catch (...) {
    return fail("GET " + url + ": not JSON");
  }
}

// Protected resource metadata (RFC 9728) -> authorization server metadata (RFC 8414).
Result<Json> discover(const std::string& url) {
  std::string as = origin(url);
  auto path = url.substr(origin(url).size());
  for (auto candidate : {origin(url) + "/.well-known/oauth-protected-resource" + path,
                         origin(url) + "/.well-known/oauth-protected-resource"}) {
    if (auto prm = get_json(candidate)) {
      auto servers = prm->value("authorization_servers", std::vector<std::string>{});
      if (!servers.empty()) as = servers.front();
      break;
    }
  }
  for (auto suffix : {"/.well-known/oauth-authorization-server", "/.well-known/openid-configuration"})
    if (auto meta = get_json(origin(as) + suffix + as.substr(origin(as).size()))) return meta;
  for (auto suffix : {"/.well-known/oauth-authorization-server", "/.well-known/openid-configuration"})
    if (auto meta = get_json(origin(as) + suffix)) return meta;
  // Last resort: conventional endpoints on the server's origin.
  return Json{{"authorization_endpoint", as + "/authorize"}, {"token_endpoint", as + "/token"},
              {"registration_endpoint", as + "/register"}};
}

Result<Json> post_form(const std::string& url, const std::vector<std::pair<std::string, std::string>>& fields) {
  http::Request req;
  req.method = "POST";
  req.url = url;
  req.body = http::form(fields);
  req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
  auto res = http::send(req);
  if (!res) return std::unexpected(res.error());
  if (res->status >= 300) return fail(std::format("token endpoint HTTP {}: {}", res->status, res->body.substr(0, 300)), int(res->status));
  try {
    return Json::parse(res->body);
  } catch (...) {
    return fail("token endpoint returned non-JSON");
  }
}

void store_tokens(Json& entry, const Json& tok) {
  entry["access_token"] = tok.value("access_token", "");
  if (tok.contains("refresh_token")) entry["refresh_token"] = tok["refresh_token"];
  entry["expires_at"] = tok.contains("expires_in") ? now_ms() / 1000 + tok["expires_in"].get<int64_t>() : int64_t(0);
}

}  // namespace

std::string pkce_challenge(const std::string& verifier) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size(), digest);
  return base64url(std::string_view(reinterpret_cast<char*>(digest), sizeof digest));
}

bool has_tokens(const std::string& name) { return load_store().contains(name); }

std::optional<std::string> access_token(const std::string& name, const McpServerConfig& cfg) {
  auto store = load_store();
  if (!store.contains(name)) return std::nullopt;
  auto& e = store[name];
  if (e.value("url", "") != cfg.url) return std::nullopt;  // server moved: log in again
  auto expires = e.value("expires_at", int64_t(0));
  if (expires == 0 || expires - 60 > now_ms() / 1000) return e.value("access_token", "");
  if (!e.contains("refresh_token")) return std::nullopt;
  auto tok = post_form(e.value("token_endpoint", ""), {{"grant_type", "refresh_token"},
                                                       {"refresh_token", e.value("refresh_token", "")},
                                                       {"client_id", e.value("client_id", "")},
                                                       {"resource", cfg.url}});
  if (!tok) {
    log::warn("mcp " + name + ": token refresh failed (" + tok.error().message + "); run `shaman mcp auth " + name + "`");
    return std::nullopt;
  }
  store_tokens(e, *tok);
  save_store(store);
  log::debug(log::Cat::mcp, "{}: refreshed access token", name);
  return e.value("access_token", "");
}

Result<void> login(const std::string& name, const McpServerConfig& cfg) {
  if (cfg.url.empty()) return fail(name + " is not a remote MCP server");
  auto meta = discover(cfg.url);
  if (!meta) return std::unexpected(meta.error());
  auto opts = cfg.oauth.is_object() ? cfg.oauth : Json::object();
  int port = opts.value("redirectPort", 19876);
  auto listener = net::Listener::bind("127.0.0.1", port);
  if (!listener) return std::unexpected(listener.error());
  std::string redirect = std::format("http://127.0.0.1:{}/callback", (*listener)->port());

  std::string client_id = opts.value("clientId", "");
  if (client_id.empty()) {
    auto reg_url = meta->value("registration_endpoint", "");
    if (reg_url.empty()) return fail("server does not support dynamic registration; set mcp." + name + ".oauth.clientId");
    http::Request req;
    req.method = "POST";
    req.url = reg_url;
    req.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
    req.body = Json{{"client_name", "shaman-cli"}, {"redirect_uris", {redirect}},
                    {"grant_types", {"authorization_code", "refresh_token"}}, {"response_types", {"code"}},
                    {"token_endpoint_auth_method", "none"}}.dump();
    auto res = http::send(req);
    if (!res) return std::unexpected(res.error());
    if (res->status >= 300) return fail(std::format("client registration HTTP {}: {}", res->status, res->body.substr(0, 300)));
    client_id = Json::parse(res->body).value("client_id", "");
  }

  auto verifier = random_token(48), state = random_token(16);
  auto url = meta->value("authorization_endpoint", "") + "?" +
             http::form({{"response_type", "code"}, {"client_id", client_id}, {"redirect_uri", redirect},
                         {"code_challenge", pkce_challenge(verifier)}, {"code_challenge_method", "S256"},
                         {"state", state}, {"resource", cfg.url}});
  if (opts.contains("scope")) url += "&scope=" + str::url_encode(opts["scope"].get<std::string>());

  std::cerr << "Opening your browser to authorise " << name << ".\nIf it doesn't open, visit:\n  " << url << "\n";
  const char* open = std::getenv("SHAMAN_OPEN");
#if defined(__APPLE__)
  std::string opener = open ? open : "open";
#elif defined(_WIN32)
  std::string opener = open ? open : "start \"\"";
#else
  std::string opener = open ? open : "xdg-open";
#endif
  process::shell(opener + " '" + url + "' >/dev/null 2>&1 &", {.timeout = std::chrono::seconds(10)});

  std::string code;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (code.empty() && std::chrono::steady_clock::now() < deadline) {
    auto c = (*listener)->accept(1000);
    if (!c) continue;
    auto req = c->read_request();
    if (!req || req->path != "/callback") {
      if (req) c->respond(404, "text/plain", "not found");
      continue;
    }
    if (req->param("state") != state) {
      c->respond(400, "text/html", "<h3>State mismatch. Try again.</h3>");
      continue;
    }
    if (auto err = req->param("error"); !err.empty()) {
      c->respond(400, "text/html", "<h3>Authorisation failed: " + err + "</h3>");
      return fail("authorisation failed: " + err);
    }
    code = req->param("code");
    c->respond(200, "text/html", "<h3>shaman is authorised. You can close this window.</h3>");
  }
  if (code.empty()) return fail("timed out waiting for the browser");

  auto tok = post_form(meta->value("token_endpoint", ""), {{"grant_type", "authorization_code"}, {"code", code},
                                                           {"redirect_uri", redirect}, {"client_id", client_id},
                                                           {"code_verifier", verifier}, {"resource", cfg.url}});
  if (!tok) return std::unexpected(tok.error());
  auto store = load_store();
  Json entry = {{"url", cfg.url}, {"client_id", client_id}, {"token_endpoint", meta->value("token_endpoint", "")}};
  store_tokens(entry, *tok);
  store[name] = entry;
  save_store(store);
  return {};
}

Result<void> logout(const std::string& name) {
  auto store = load_store();
  if (!store.contains(name)) return fail("not logged in to " + name);
  store.erase(name);
  save_store(store);
  return {};
}

}  // namespace shaman::mcp::oauth
