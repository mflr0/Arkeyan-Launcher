#include "launcher/online/local_addresses.h"
#include "launcher/online/net_socket.h"
#include <algorithm>
#include <array>
#include <cctype>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

namespace online
{
    namespace
    {
        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }
        std::array<uint8_t, 4> Ipv4(const std::string& address)
        {
            std::array<uint8_t, 4> bytes{};
            net::Startup();
            if (inet_pton(AF_INET, address.c_str(), bytes.data()) != 1)
                bytes = {};
            return bytes;
        }
        bool Bridge(const LocalAddress& address)
        {
            const std::string name = Lower(address.name + " " + address.description);
            constexpr std::string_view skipped[] = { "docker", "br-", "virbr", "veth", "vmnet", "vboxnet", "bridge",
                "virtualbox", "vmware", "hyper-v", "vethernet (wsl", "vethernet (default switch)" };
            return std::any_of(std::begin(skipped), std::end(skipped),
                [&](auto word) { return name.find(word) != std::string::npos; });
        }
    }

    LocalNetwork NetworkFor(const LocalAddress& address)
    {
        const auto ip = Ipv4(address.address);
        const auto name = Lower(address.name + " " + address.description);
        if (ip[0] == 26 || name.find("radmin") != std::string::npos)
            return LocalNetwork::Radmin;
        if (ip[0] == 25 || name.find("hamachi") != std::string::npos)
            return LocalNetwork::Hamachi;
        if ((ip[0] == 100 && ip[1] >= 64 && ip[1] <= 127) || name.find("tailscale") != std::string::npos)
            return LocalNetwork::Tailscale;
        if (name.find("zerotier") != std::string::npos || Lower(address.name).starts_with("zt"))
            return LocalNetwork::ZeroTier;
        if (name.find("tun") != std::string::npos || name.find("tap") != std::string::npos ||
            name.find("vpn") != std::string::npos || Lower(address.name).starts_with("wg"))
            return LocalNetwork::Virtual;
        return LocalNetwork::Home;
    }

    std::vector<LocalAddress> SelectLocalAddresses(std::span<const LocalAddress> interfaces, std::string_view preferred)
    {
        std::vector<LocalAddress> chosen;
        for (const auto& address : interfaces)
        {
            const auto ip = Ipv4(address.address);
            if (!address.up || !ip[0] || ip[0] == 127 || ip[0] >= 224 || (ip[0] == 169 && ip[1] == 254) ||
                Bridge(address))
                continue;
            chosen.push_back(address);
        }
        auto lan = std::find_if(chosen.begin(), chosen.end(), [&](const auto& address) {
            return address.address == preferred && NetworkFor(address) == LocalNetwork::Home;
        });
        if (lan == chosen.end())
            lan = std::find_if(chosen.begin(), chosen.end(),
                [](const auto& address) { return NetworkFor(address) == LocalNetwork::Home; });
        // The LAN must survive the cap even when virtual adapters are enumerated first.
        if (lan != chosen.end())
            std::rotate(chosen.begin(), lan, lan + 1);
        std::vector<LocalAddress> result;
        for (const auto& address : chosen)
            if (result.size() < 4 && std::none_of(result.begin(), result.end(),
                                         [&](const auto& other) { return address.address == other.address; }))
                result.push_back(address);
        return result;
    }

    std::vector<LocalAddress> LocalAddresses()
    {
        std::vector<LocalAddress> interfaces;
        net::Startup();
#if defined(_WIN32)
        ULONG size = 16384;
        std::vector<uint8_t> storage(size);
        ULONG result;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
            result = GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
            if (result == ERROR_BUFFER_OVERFLOW)
            {
                storage.resize(size);
                continue;
            }
            if (result != NO_ERROR)
                break;
            const auto utf8 = [](const wchar_t* text) {
                if (!text)
                    return std::string{};
                const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
                std::string value(size_t(std::max(count, 1)), '\0');
                if (count)
                    WideCharToMultiByte(CP_UTF8, 0, text, -1, value.data(), count, nullptr, nullptr);
                value.pop_back();
                return value;
            };
            for (auto* a = adapters; a; a = a->Next)
            {
                if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                    continue;
                for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
                {
                    if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET)
                        continue;
                    char ip[INET_ADDRSTRLEN] = {};
                    const auto* address = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
                    if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof ip))
                        interfaces.push_back({ utf8(a->FriendlyName), utf8(a->Description), ip });
                }
            }
            break;
        }
#else
        ifaddrs* addresses = nullptr;
        if (getifaddrs(&addresses) == 0)
        {
            for (auto* a = addresses; a; a = a->ifa_next)
            {
                if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) ||
                    (a->ifa_flags & IFF_LOOPBACK))
                    continue;
                char ip[INET_ADDRSTRLEN] = {};
                const auto* address = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
                if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof ip))
                    interfaces.push_back({ a->ifa_name, "", ip });
            }
            freeifaddrs(addresses);
        }
#endif
        return SelectLocalAddresses(interfaces);
    }
}
