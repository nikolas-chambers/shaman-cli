#pragma once

#include <optional>
#include <string>

#include "shaman/config/config.hpp"
#include "shaman/core/result.hpp"

// OAuth 2.1 for remote MCP servers: protected-resource and authorization-server
// discovery, dynamic client registration, PKCE (S256) with a loopback
// redirect, token refresh. Tokens live in $XDG_DATA_HOME/shaman/mcp-auth.json (0600).
namespace shaman::mcp::oauth {

// A valid access token for the server, refreshing it if needed. nullopt when
// the user has not logged in (or the refresh failed).
std::optional<std::string> access_token(const std::string& name, const McpServerConfig& cfg);

// Interactive login: opens the browser (or prints the URL) and waits for the
// redirect. $SHAMAN_OPEN overrides the command used to open URLs.
Result<void> login(const std::string& name, const McpServerConfig& cfg);
Result<void> logout(const std::string& name);
bool has_tokens(const std::string& name);

std::string pkce_challenge(const std::string& verifier);  // base64url(sha256(verifier))

}  // namespace shaman::mcp::oauth
