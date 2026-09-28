#include "shaman/http/http.hpp"

#include <curl/curl.h>

#include <chrono>
#include <cstdlib>
#include <mutex>

#include "shaman/core/log.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::http {
namespace {

void global_init() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Ctx {
  CURL* curl;
  const Request* req;
  const std::function<bool(std::string_view)>* on_data;
  Response* res;
  bool aborted = false;
};

size_t on_write(char* ptr, size_t size, size_t n, void* user) {
  auto* ctx = static_cast<Ctx*>(user);
  size_t len = size * n;
  long status = 0;
  curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &status);
  if (status >= 300 || !ctx->on_data) {
    ctx->res->body.append(ptr, len);
    return len;
  }
  if (!(*ctx->on_data)(std::string_view(ptr, len))) {
    ctx->aborted = true;
    return 0;
  }
  return len;
}

size_t on_header(char* ptr, size_t size, size_t n, void* user) {
  auto* res = static_cast<Response*>(user);
  std::string_view line(ptr, size * n);
  auto colon = line.find(':');
  if (colon != std::string_view::npos) {
    std::string name(line.substr(0, colon));
    for (auto& ch : name) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.remove_suffix(1);
    res->headers.emplace_back(std::move(name), std::string(value));
  }
  return size * n;
}

int on_progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto* ctx = static_cast<Ctx*>(user);
  return ctx->req->cancel && ctx->req->cancel->load() ? 1 : 0;
}

Result<Response> perform(const Request& req, const std::function<bool(std::string_view)>* on_data) {
  global_init();
  CURL* curl = curl_easy_init();
  if (!curl) return fail("curl init failed");
  Response res;
  Ctx ctx{curl, &req, on_data, &res};

  curl_slist* headers = nullptr;
  for (auto& [k, v] : req.headers) headers = curl_slist_append(headers, (k + ": " + v).c_str());
  curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (!req.body.empty() || req.method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(req.body.size()));
  }
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeout_s);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "shaman-cli/0.1");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
  // libcurl ignores the curl CLI's CA env vars; honour them for proxies and corporate CAs.
  for (const char* var : {"SHAMAN_CA_BUNDLE", "CURL_CA_BUNDLE", "SSL_CERT_FILE"})
    if (const char* ca = std::getenv(var); ca && *ca) {
      curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
      break;
    }

  auto t0 = std::chrono::steady_clock::now();
  log::debug(log::Cat::http, "{} {} ({} bytes)", req.method, req.url, req.body.size());
  CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  log::debug(log::Cat::http, "{} {} -> {} in {}ms", req.method, req.url, res.status, ms);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK && !ctx.aborted) {
    if (req.cancel && req.cancel->load()) return fail("cancelled");
    return fail(std::string("network error: ") + curl_easy_strerror(rc), 0, true);
  }
  return res;
}

}  // namespace

Result<Response> send(const Request& req) { return perform(req, nullptr); }

std::string Response::header(const std::string& name) const {
  for (auto it = headers.rbegin(); it != headers.rend(); ++it)  // last wins (after redirects)
    if (it->first == name) return it->second;
  return "";
}

std::string form(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out;
  for (auto& [k, v] : fields) {
    if (!out.empty()) out += '&';
    out += str::url_encode(k) + "=" + str::url_encode(v);
  }
  return out;
}

Result<Response> stream(const Request& req, const std::function<bool(std::string_view)>& on_data) {
  return perform(req, &on_data);
}

}  // namespace shaman::http
