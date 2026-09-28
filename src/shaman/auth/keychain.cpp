#include "shaman/auth/keychain.hpp"

#include <cstdlib>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#endif

namespace shaman::auth::keychain {

namespace {
constexpr const char* kService = "shaman";
}

#if defined(_WIN32)

static std::wstring target(const std::string& account) {
  std::string t = std::string(kService) + ":" + account;
  return std::wstring(t.begin(), t.end());  // account ids are ASCII provider names
}

Result<std::string> backend() { return std::string("Windows Credential Manager"); }

Result<void> set(const std::string& account, const std::string& secret) {
  auto t = target(account);
  CREDENTIALW c{};
  c.Type = CRED_TYPE_GENERIC;
  c.TargetName = t.data();
  c.CredentialBlobSize = DWORD(secret.size());
  c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
  c.Persist = CRED_PERSIST_LOCAL_MACHINE;
  std::wstring user = L"shaman";
  c.UserName = user.data();
  if (!CredWriteW(&c, 0)) return fail("CredWrite failed", int(GetLastError()));
  return {};
}

std::optional<std::string> get(const std::string& account) {
  PCREDENTIALW c = nullptr;
  if (!CredReadW(target(account).c_str(), CRED_TYPE_GENERIC, 0, &c)) return std::nullopt;
  std::string out(reinterpret_cast<char*>(c->CredentialBlob), c->CredentialBlobSize);
  CredFree(c);
  return out;
}

Result<void> erase(const std::string& account) {
  if (!CredDeleteW(target(account).c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND)
    return fail("CredDelete failed", int(GetLastError()));
  return {};
}

#elif defined(__APPLE__)

namespace {
struct CF {  // owns a CoreFoundation reference
  CFTypeRef ref = nullptr;
  ~CF() {
    if (ref) CFRelease(ref);
  }
};
CFStringRef str(const std::string& s) { return CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8); }

CFMutableDictionaryRef query(const std::string& account) {
  auto q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CF service{str(kService)}, acct{str(account)};
  CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(q, kSecAttrService, service.ref);
  CFDictionarySetValue(q, kSecAttrAccount, acct.ref);
  return q;
}
}  // namespace

Result<std::string> backend() { return std::string("macOS Keychain"); }

Result<void> set(const std::string& account, const std::string& secret) {
  CF q{query(account)};
  SecItemDelete(static_cast<CFDictionaryRef>(q.ref));
  CF data{CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(secret.data()), CFIndex(secret.size()))};
  CFDictionarySetValue(static_cast<CFMutableDictionaryRef>(const_cast<void*>(q.ref)), kSecValueData, data.ref);
  OSStatus s = SecItemAdd(static_cast<CFDictionaryRef>(q.ref), nullptr);
  if (s != errSecSuccess) return fail("Keychain error " + std::to_string(int(s)));
  return {};
}

std::optional<std::string> get(const std::string& account) {
  CF q{query(account)};
  auto mq = static_cast<CFMutableDictionaryRef>(const_cast<void*>(q.ref));
  CFDictionarySetValue(mq, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(mq, kSecMatchLimit, kSecMatchLimitOne);
  CF result;
  if (SecItemCopyMatching(static_cast<CFDictionaryRef>(q.ref), &result.ref) != errSecSuccess || !result.ref) return std::nullopt;
  auto data = static_cast<CFDataRef>(result.ref);
  return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), size_t(CFDataGetLength(data)));
}

Result<void> erase(const std::string& account) {
  CF q{query(account)};
  OSStatus s = SecItemDelete(static_cast<CFDictionaryRef>(q.ref));
  if (s != errSecSuccess && s != errSecItemNotFound) return fail("Keychain error " + std::to_string(int(s)));
  return {};
}

#else  // Linux and other Unix: the freedesktop Secret Service via secret-tool (libsecret)

Result<std::string> backend() {
  if (process::termux()) return fail("no secret store on Android");
  if (!process::which("secret-tool")) return fail("secret-tool not installed (libsecret-tools)");
  const char* bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
  if (!bus || !*bus) return fail("no desktop session (D-Bus) for the Secret Service");
  return std::string("Secret Service");
}

static std::string q(const std::string& s) { return "'" + str::replace_all(s, "'", "'\\''") + "'"; }

Result<void> set(const std::string& account, const std::string& secret) {
  if (auto b = backend(); !b) return std::unexpected(b.error());
  process::Options o{.timeout = std::chrono::seconds(20)};
  o.input = secret;  // on stdin, never on the command line
  auto r = process::shell("secret-tool store --label=" + q("shaman " + account) + " service shaman account " + q(account), o);
  if (!r || r->exit_code != 0) return fail("secret-tool store failed" + (r ? ": " + str::trim(r->output) : ""));
  return {};
}

std::optional<std::string> get(const std::string& account) {
  if (!backend()) return std::nullopt;
  process::Options o{.timeout = std::chrono::seconds(20)};
  o.separate_stderr = true;
  auto r = process::shell("secret-tool lookup service shaman account " + q(account), o);
  if (!r || r->exit_code != 0 || r->output.empty()) return std::nullopt;
  auto out = r->output;
  if (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

Result<void> erase(const std::string& account) {
  if (auto b = backend(); !b) return std::unexpected(b.error());
  process::shell("secret-tool clear service shaman account " + q(account), {.timeout = std::chrono::seconds(20)});
  return {};
}

#endif

}  // namespace shaman::auth::keychain
