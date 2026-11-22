#pragma once

// The host's public address, as the internet sees it (docs/online.md, "Room codes"): one STUN binding request (RFC 5389)
// to a free public server, over IPv4 and over IPv6. Only the address in the answer is used; nothing else is sent or kept.
// Lookup blocks for a few seconds at most: the room's router worker calls it.
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace online::stun
{
    using Id = std::array<uint8_t, 12>;

    struct Endpoint
    {
        std::string ip; // "" when unknown
        uint16_t port = 0;
    };

    // Tried in order until one answers.
    inline constexpr std::array<std::string_view, 2> Servers{ "stun.l.google.com:19302", "stun.cloudflare.com:3478" };

    std::vector<uint8_t> BindingRequest(const Id& id);
    // The mapped address of a success answer to the request `id` (XOR-MAPPED-ADDRESS, else MAPPED-ADDRESS).
    bool ParseBindingResponse(std::span<const uint8_t> packet, const Id& id, Endpoint& mapped);

    struct Result
    {
        Endpoint v4, v6;
    };
    Result Lookup(std::chrono::milliseconds perServer = std::chrono::milliseconds(1500));
}
