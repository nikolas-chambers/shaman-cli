#pragma once

#include <string>

namespace shaman::session {

// Mask credentials in text before it reaches a model: cloud and API keys,
// tokens, private keys, and password/secret assignments. Returns the number
// of redactions. Config: "redact_secrets": false turns it off.
int redact_secrets(std::string& text);

}  // namespace shaman::session
