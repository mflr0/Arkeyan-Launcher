#pragma once

#include "launcher/online/net_socket.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace online
{
    // Numeric addresses keep joining independent of DNS and of the host's interface names.
    std::string EncodeRoomCode(std::span<const net::Endpoint> endpoints, uint64_t secret);
    bool DecodeRoomCode(std::string_view text, std::vector<net::Endpoint>& endpoints, uint64_t& secret);
}
