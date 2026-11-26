#include "launcher/online/net_socket.h"
#include "launcher/online/room_code.h"
#include <charconv>
#include <algorithm>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace online::net
{
    namespace
    {
#if defined(_WIN32)
        using Raw = SOCKET;
        const Raw NoSocket = INVALID_SOCKET;
        Raw ToRaw(Handle handle) { return Raw(handle); }
        Handle Wrap(Raw raw) { return raw == INVALID_SOCKET ? Invalid : Handle(raw); }
        void CloseRaw(Raw raw) { closesocket(raw); }
        bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
        void NonBlocking(Raw raw)
        {
            u_long on = 1;
            ioctlsocket(raw, FIONBIO, &on);
        }
        std::string LastError() { return "error " + std::to_string(WSAGetLastError()); }
#else
        using Raw = int;
        const Raw NoSocket = -1;
        Raw ToRaw(Handle handle) { return Raw(handle); }
        Handle Wrap(Raw raw) { return raw < 0 ? Invalid : Handle(raw); }
        void CloseRaw(Raw raw) { close(raw); }
        bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
        void NonBlocking(Raw raw) { fcntl(raw, F_SETFL, fcntl(raw, F_GETFL, 0) | O_NONBLOCK); }
        std::string LastError() { return std::strerror(errno); }
#endif

        std::string AddressText(const sockaddr* address)
        {
            char text[INET6_ADDRSTRLEN] = {};
            if (address->sa_family == AF_INET)
                inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(address)->sin_addr, text, sizeof(text));
            else if (address->sa_family == AF_INET6)
                inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr, text, sizeof(text));
            std::string result(text);
            if (result.starts_with("::ffff:"))
                result.erase(0, 7);
            return result;
        }
    }

    bool Startup()
    {
#if defined(_WIN32)
        static std::once_flag once;
        static bool ok = false;
        std::call_once(once, [] {
            WSADATA data;
            ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        });
        return ok;
#else
        return true;
#endif
    }

    Handle BindUdp(uint16_t port, std::string& error)
    {
        if (!Startup())
            return Invalid;
        for (const int family : { AF_INET6, AF_INET })
        {
            const Raw raw = socket(family, SOCK_DGRAM, IPPROTO_UDP);
            if (raw == NoSocket)
                continue;
            int bound = -1;
            if (family == AF_INET6)
            {
                const int off = 0;
                if (setsockopt(raw, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof(off)) != 0)
                {
                    CloseRaw(raw);
                    continue;
                }
                sockaddr_in6 address{};
                address.sin6_family = AF_INET6;
                address.sin6_addr = in6addr_any;
                address.sin6_port = htons(port);
                bound = bind(raw, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            }
            else
            {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_ANY);
                address.sin_port = htons(port);
                bound = bind(raw, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            }
            if (bound == 0)
            {
                NonBlocking(raw);
                return Wrap(raw);
            }
            error = LastError();
            CloseRaw(raw);
        }
        return Invalid;
    }

    uint16_t LocalPort(Handle handle)
    {
        sockaddr_storage address{};
        socklen_t size = sizeof(address);
        if (getsockname(ToRaw(handle), reinterpret_cast<sockaddr*>(&address), &size) != 0)
            return 0;
        return ntohs(address.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&address)->sin6_port
                                                   : reinterpret_cast<sockaddr_in*>(&address)->sin_port);
    }

    std::vector<Endpoint> Resolve(const std::string& host, uint16_t port, bool numeric)
    {
        std::vector<Endpoint> result;
        if (!Startup() || !port)
            return result;
        addrinfo hints{}, *found = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = numeric ? AI_NUMERICHOST : 0;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0)
            return result;
        for (auto* each = found; each; each = each->ai_next)
        {
            Endpoint endpoint{ AddressText(each->ai_addr), port };
            if (!endpoint.ip.empty() && std::find(result.begin(), result.end(), endpoint) == result.end())
                result.push_back(std::move(endpoint));
        }
        freeaddrinfo(found);
        return result;
    }

    std::string Address(const Endpoint& endpoint)
    {
        return (endpoint.ip.find(':') == std::string::npos ? endpoint.ip : "[" + endpoint.ip + "]") + ":" +
               std::to_string(endpoint.port);
    }

    long ReceiveFrom(Handle handle, uint8_t* bytes, size_t capacity, Endpoint& peer)
    {
        sockaddr_storage address{};
        socklen_t size = sizeof(address);
        const auto got = recvfrom(ToRaw(handle), reinterpret_cast<char*>(bytes), int(capacity), 0,
            reinterpret_cast<sockaddr*>(&address), &size);
        if (got < 0)
            return WouldBlock() ? -1 : -2;
        peer.ip = AddressText(reinterpret_cast<sockaddr*>(&address));
        peer.port = ntohs(address.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&address)->sin6_port
                                                        : reinterpret_cast<sockaddr_in*>(&address)->sin_port);
        return long(got);
    }

    long SendTo(Handle handle, std::span<const uint8_t> bytes, const Endpoint& peer)
    {
        sockaddr_storage local{};
        socklen_t size = sizeof(local);
        if (getsockname(ToRaw(handle), reinterpret_cast<sockaddr*>(&local), &size) != 0)
            return -1;
        sockaddr_storage address{};
        socklen_t length = 0;
        if (local.ss_family == AF_INET6)
        {
            auto* a = reinterpret_cast<sockaddr_in6*>(&address);
            a->sin6_family = AF_INET6;
            a->sin6_port = htons(peer.port);
            if (peer.ip.find(':') == std::string::npos)
            {
                a->sin6_addr.s6_addr[10] = a->sin6_addr.s6_addr[11] = 0xff;
                if (inet_pton(AF_INET, peer.ip.c_str(), a->sin6_addr.s6_addr + 12) != 1)
                    return -1;
            }
            else if (inet_pton(AF_INET6, peer.ip.c_str(), &a->sin6_addr) != 1)
                return -1;
            length = sizeof(*a);
        }
        else
        {
            auto* a = reinterpret_cast<sockaddr_in*>(&address);
            a->sin_family = AF_INET;
            a->sin_port = htons(peer.port);
            if (inet_pton(AF_INET, peer.ip.c_str(), &a->sin_addr) != 1)
                return -1;
            length = sizeof(*a);
        }
        const auto sent = sendto(ToRaw(handle), reinterpret_cast<const char*>(bytes.data()), int(bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&address), length);
        return sent >= 0 ? long(sent) : WouldBlock() ? 0 : -1;
    }

    void Close(Handle handle)
    {
        if (handle != Invalid)
            CloseRaw(ToRaw(handle));
    }

    bool ParseAddress(std::string_view text, uint16_t defaultPort, std::string& host, uint16_t& port)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);
        while (
            !text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
            text.remove_suffix(1);
        if (text.empty())
            return false;
        uint64_t secret = 0;
        std::vector<Endpoint> endpoints;
        if (DecodeRoomCode(text, endpoints, secret))
        {
            host = endpoints.front().ip;
            port = endpoints.front().port;
            return true;
        }
        std::string_view portText;
        if (text.front() == '[')
        {
            const size_t close = text.find(']');
            if (close == std::string_view::npos)
                return false;
            host = std::string(text.substr(1, close - 1));
            const auto rest = text.substr(close + 1);
            if (!rest.empty())
            {
                if (rest.front() != ':')
                    return false;
                portText = rest.substr(1);
            }
        }
        else
        {
            const size_t colon = text.find(':');
            // Two colons or more without brackets: a bare IPv6 address.
            if (colon != std::string_view::npos && text.find(':', colon + 1) == std::string_view::npos)
            {
                host = std::string(text.substr(0, colon));
                portText = text.substr(colon + 1);
            }
            else
                host = std::string(text);
        }
        if (host.empty() || host.find_first_of(" /\\") != std::string::npos)
            return false;
        port = defaultPort;
        if (!portText.empty())
        {
            unsigned value = 0;
            const auto [end, error] = std::from_chars(portText.data(), portText.data() + portText.size(), value);
            if (error != std::errc{} || end != portText.data() + portText.size() || value == 0 || value > 65535)
                return false;
            port = uint16_t(value);
        }
        else if (text.back() == ':')
            return false;
        return true;
    }
}
