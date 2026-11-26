#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace online
{
    struct LocalAddress
    {
        std::string name, description, address;
        bool up = true;
        bool operator==(const LocalAddress&) const = default;
    };
    enum class LocalNetwork
    {
        Home,
        Radmin,
        Hamachi,
        Tailscale,
        ZeroTier,
        Virtual,
    };
    LocalNetwork NetworkFor(const LocalAddress& address);
    std::vector<LocalAddress> SelectLocalAddresses(
        std::span<const LocalAddress> interfaces, std::string_view preferred = {});
    std::vector<LocalAddress> LocalAddresses();
}
