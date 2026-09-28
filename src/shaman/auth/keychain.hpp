#pragma once

#include <optional>
#include <string>

#include "shaman/core/result.hpp"

// The operating system's secret store: macOS Keychain, Windows Credential Manager, or the Secret Service
// on Linux (GNOME Keyring, KWallet) through `secret-tool`. Entries are "shaman" / <account>.
namespace shaman::auth::keychain {

// The store in use here ("macOS Keychain", ...), or why there is none.
Result<std::string> backend();

Result<void> set(const std::string& account, const std::string& secret);
std::optional<std::string> get(const std::string& account);
Result<void> erase(const std::string& account);

}  // namespace shaman::auth::keychain
