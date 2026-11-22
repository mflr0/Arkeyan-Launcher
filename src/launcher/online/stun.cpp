#include "launcher/online/stun.h"
#include "launcher/online/lobby.h"
#include "launcher/online/net_socket.h"
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace online::stun
{
    namespace
    {
        constexpr uint16_t BindingRequestType = 0x0001, BindingSuccess = 0x0101;
        constexpr uint16_t MappedAddress = 0x0001, XorMappedAddress = 0x0020;
        constexpr uint32_t MagicCookie = 0x2112A442;
        constexpr size_t HeaderBytes = 20;

        uint16_t Read16(std::span<const uint8_t> bytes, size_t at)
        {
            return uint16_t((bytes[at] << 8) | bytes[at + 1]);
        }
        uint32_t Read32(std::span<const uint8_t> bytes, size_t at)
        {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) | (uint32_t(bytes[at + 2]) << 8) |
                   bytes[at + 3];
        }

        bool ParseAddress(std::span<const uint8_t> value, bool xored, const Id& id, Endpoint& mapped)
        {
            if (value.size() < 8)
                return false;
            const uint8_t family = value[1];
            uint16_t port = Read16(value, 2);
            std::array<uint8_t, 16> address{};
            size_t size = 0;
            if (family == 0x01)
                size = 4;
            else if (family == 0x02 && value.size() >= 20)
                size = 16;
            else
                return false;
            std::memcpy(address.data(), value.data() + 4, size);
            if (xored)
            {
                port ^= uint16_t(MagicCookie >> 16);
                std::array<uint8_t, 16> key{};
                for (int i = 0; i < 4; ++i)
                    key[size_t(i)] = uint8_t(MagicCookie >> (24 - 8 * i));
                std::memcpy(key.data() + 4, id.data(), id.size());
                for (size_t i = 0; i < size; ++i)
                    address[i] ^= key[i];
            }
            char text[INET6_ADDRSTRLEN] = {};
            if (!inet_ntop(size == 4 ? AF_INET : AF_INET6, address.data(), text, sizeof(text)))
                return false;
            mapped.ip = text;
            mapped.port = port;
            return true;
        }

#if defined(_WIN32)
        using Raw = SOCKET;
        const Raw NoSocket = INVALID_SOCKET;
        void CloseRaw(Raw raw) { closesocket(raw); }
        int Wait(Raw raw, int timeoutMs)
        {
            WSAPOLLFD fd{ raw, POLLIN, 0 };
            return WSAPoll(&fd, 1, timeoutMs);
        }
#else
        using Raw = int;
        const Raw NoSocket = -1;
        void CloseRaw(Raw raw) { close(raw); }
        int Wait(Raw raw, int timeoutMs)
        {
            pollfd fd{ raw, POLLIN, 0 };
            return poll(&fd, 1, timeoutMs);
        }
#endif

        bool Ask(std::string_view server, int family, std::chrono::milliseconds timeout, Endpoint& mapped)
        {
            const size_t colon = server.rfind(':');
            const std::string name(server.substr(0, colon)), service(server.substr(colon + 1));
            addrinfo hints{}, *found = nullptr;
            hints.ai_family = family;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            if (getaddrinfo(name.c_str(), service.c_str(), &hints, &found) != 0 || !found)
                return false;
            bool ok = false;
            const Raw raw = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
            if (raw != NoSocket)
            {
                Id id{};
                RandomBytes(id);
                const auto request = BindingRequest(id);
                // Three tries in the time given: a lost datagram is sent again.
                const int slice = int(timeout.count() / 3);
                for (int attempt = 0; attempt < 3 && !ok; ++attempt)
                {
                    if (sendto(raw, reinterpret_cast<const char*>(request.data()), int(request.size()), 0,
                            found->ai_addr, socklen_t(found->ai_addrlen)) < 0)
                        break;
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(slice);
                    for (;;)
                    {
                        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now())
                                              .count();
                        if (left <= 0 || Wait(raw, int(left)) <= 0)
                            break;
                        uint8_t buffer[512];
                        const auto got = recv(raw, reinterpret_cast<char*>(buffer), int(sizeof(buffer)), 0);
                        if (got <= 0)
                            break;
                        if (ParseBindingResponse(std::span<const uint8_t>(buffer, size_t(got)), id, mapped))
                        {
                            ok = true;
                            break;
                        }
                    }
                }
                CloseRaw(raw);
            }
            freeaddrinfo(found);
            return ok;
        }
    }

    std::vector<uint8_t> BindingRequest(const Id& id)
    {
        std::vector<uint8_t> packet(HeaderBytes, 0);
        packet[0] = uint8_t(BindingRequestType >> 8);
        packet[1] = uint8_t(BindingRequestType);
        for (int i = 0; i < 4; ++i)
            packet[size_t(4 + i)] = uint8_t(MagicCookie >> (24 - 8 * i));
        std::memcpy(packet.data() + 8, id.data(), id.size());
        return packet;
    }

    bool ParseBindingResponse(std::span<const uint8_t> packet, const Id& id, Endpoint& mapped)
    {
        if (packet.size() < HeaderBytes || Read16(packet, 0) != BindingSuccess || Read32(packet, 4) != MagicCookie ||
            std::memcmp(packet.data() + 8, id.data(), id.size()) != 0)
            return false;
        const size_t length = Read16(packet, 2);
        if (length % 4 != 0 || HeaderBytes + length > packet.size())
            return false;
        Endpoint plain;
        bool havePlain = false;
        for (size_t at = HeaderBytes; at + 4 <= HeaderBytes + length;)
        {
            const uint16_t type = Read16(packet, at), size = Read16(packet, at + 2);
            if (at + 4 + size > HeaderBytes + length)
                return false;
            const auto value = packet.subspan(at + 4, size);
            if (type == XorMappedAddress && ParseAddress(value, true, id, mapped))
                return true;
            if (type == MappedAddress && !havePlain)
                havePlain = ParseAddress(value, false, id, plain);
            at += 4 + ((size + 3u) & ~3u);
        }
        if (havePlain)
            mapped = plain;
        return havePlain;
    }

    Result Lookup(std::chrono::milliseconds perServer)
    {
        Result result;
        if (!net::Startup())
            return result;
        for (const auto server : Servers)
            if (Ask(server, AF_INET, perServer, result.v4))
                break;
        for (const auto server : Servers)
            if (Ask(server, AF_INET6, perServer, result.v6))
                break;
        return result;
    }
}
