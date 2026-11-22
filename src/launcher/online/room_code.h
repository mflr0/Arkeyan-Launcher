#pragma once

// A room code carries the address and a 40-bit room secret, followed by a typo check.
// Crockford base 32, grouped by four: 20 letters for IPv4 on the default port,
// 24 for IPv4 with a port, 44 for IPv6 with a port. See docs/online.md.
#include <cstdint>
#include <string>
#include <string_view>

namespace online
{
    // "" when `host` is not an IPv4 or IPv6 address.
    std::string EncodeRoomCode(std::string_view host, uint16_t port, uint64_t secret);
    // True when `text` is a room code (dashes, spaces and case ignored), with the address it holds: `host` as
    // "a.b.c.d" or an IPv6 address without brackets.
    bool DecodeRoomCode(std::string_view text, std::string& host, uint16_t& port, uint64_t& secret);
}
