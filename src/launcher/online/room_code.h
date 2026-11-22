#pragma once

// The room code (docs/online.md, "Room codes"): the host's address written as a short code a player copies and pastes,
// such as K7QD-M2XA. No server is involved: the code is the address itself, in base 32 (Crockford's letters, so O and
// 0, I, L and 1 read the same), with a check that catches a typo. Three lengths: an IPv4 address on the default port
// (8 letters), an IPv4 address with its port (12), an IPv6 address with its port (32).
#include <cstdint>
#include <string>
#include <string_view>

namespace online
{
    // "" when `host` is not an IPv4 or IPv6 address.
    std::string EncodeRoomCode(std::string_view host, uint16_t port);
    // True when `text` is a room code (dashes, spaces and case ignored), with the address it holds: `host` as
    // "a.b.c.d" or an IPv6 address without brackets.
    bool DecodeRoomCode(std::string_view text, std::string& host, uint16_t& port);
}
