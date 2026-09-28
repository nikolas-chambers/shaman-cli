#pragma once

#include <string>

// Pairing a phone or another device with a running server (`shaman serve --pair`).
namespace shaman::server {

// This machine's address on the local network (the one used for outgoing traffic), or "127.0.0.1".
std::string lan_ip();

// `text` as a QR code drawn with half-block characters (two modules per character cell), with a quiet zone.
// ASCII fallback uses "##" per module.
std::string qr_terminal(const std::string& text, bool unicode = true);

}  // namespace shaman::server
