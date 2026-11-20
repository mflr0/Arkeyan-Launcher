#include "launcher/online/net_socket.h"
#include <charconv>
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
#include <netinet/tcp.h>
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
        int PollOne(Raw raw, short events)
        {
            WSAPOLLFD fd{ raw, events, 0 };
            return WSAPoll(&fd, 1, 0);
        }
#else
        using Raw = int;
        const Raw NoSocket = -1;
        Raw ToRaw(Handle handle) { return Raw(handle); }
        Handle Wrap(Raw raw) { return raw < 0 ? Invalid : Handle(raw); }
        void CloseRaw(Raw raw) { close(raw); }
        bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
        void NonBlocking(Raw raw) { fcntl(raw, F_SETFL, fcntl(raw, F_GETFL, 0) | O_NONBLOCK); }
        std::string LastError() { return std::strerror(errno); }
        int PollOne(Raw raw, short events)
        {
            pollfd fd{ raw, events, 0 };
            return poll(&fd, 1, 0);
        }
#endif

        void Configure(Raw raw)
        {
            NonBlocking(raw);
            const int on = 1;
            // Lobby messages are small and the players wait on each one.
            setsockopt(raw, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
#if defined(__APPLE__)
            setsockopt(raw, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        }

        std::string AddressText(const sockaddr* address)
        {
            char text[INET6_ADDRSTRLEN] = {};
            if (address->sa_family == AF_INET)
                inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(address)->sin_addr, text, sizeof(text));
            else if (address->sa_family == AF_INET6)
                inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr, text, sizeof(text));
            return text;
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

    Handle Listen(uint16_t port, std::string& error)
    {
        if (!Startup())
        {
            error = "no network";
            return Invalid;
        }
        // IPv6 first, dual-stack, so an IPv4 guest and an IPv6 one both reach it; IPv4 alone where IPv6 is off.
        for (const int family : { AF_INET6, AF_INET })
        {
            const Raw raw = socket(family, SOCK_STREAM, IPPROTO_TCP);
            if (raw == NoSocket)
                continue;
            const int on = 1, off = 0;
#if !defined(_WIN32)
            setsockopt(raw, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#else
            (void)on;
#endif
            int bound = -1;
            if (family == AF_INET6)
            {
                setsockopt(raw, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof(off));
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
            if (bound == 0 && listen(raw, 4) == 0)
            {
                Configure(raw);
                return Wrap(raw);
            }
            error = LastError();
            CloseRaw(raw);
        }
        return Invalid;
    }

    Handle Accept(Handle listener, std::string& peer)
    {
        sockaddr_storage address{};
        socklen_t size = sizeof(address);
        const Raw raw = accept(ToRaw(listener), reinterpret_cast<sockaddr*>(&address), &size);
        if (raw == NoSocket)
            return Invalid;
        Configure(raw);
        peer = AddressText(reinterpret_cast<const sockaddr*>(&address));
        return Wrap(raw);
    }

    Handle Connect(const std::string& host, uint16_t port, std::string& error)
    {
        if (!Startup())
        {
            error = "no network";
            return Invalid;
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* found = nullptr;
        const std::string service = std::to_string(port);
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || !found)
        {
            error = "unknown address";
            return Invalid;
        }
        Handle result = Invalid;
        for (const addrinfo* each = found; each && result == Invalid; each = each->ai_next)
        {
            const Raw raw = socket(each->ai_family, each->ai_socktype, each->ai_protocol);
            if (raw == NoSocket)
                continue;
            Configure(raw);
            if (connect(raw, each->ai_addr, socklen_t(each->ai_addrlen)) == 0 || WouldBlock())
                result = Wrap(raw);
            else
            {
                error = LastError();
                CloseRaw(raw);
            }
        }
        freeaddrinfo(found);
        return result;
    }

    Progress Connecting(Handle handle)
    {
        const Raw raw = ToRaw(handle);
        const int ready = PollOne(raw, POLLOUT);
        if (ready == 0)
            return Progress::Pending;
        int error = 0;
        socklen_t size = sizeof(error);
        if (ready < 0 || getsockopt(raw, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 ||
            error != 0)
            return Progress::Failed;
        return Progress::Connected;
    }

    long Receive(Handle handle, uint8_t* buffer, size_t capacity)
    {
        const auto got = recv(ToRaw(handle), reinterpret_cast<char*>(buffer), int(capacity), 0);
        if (got > 0)
            return long(got);
        if (got == 0)
            return 0;
        return WouldBlock() ? -1 : -2;
    }

    long Send(Handle handle, const uint8_t* bytes, size_t size)
    {
#if defined(__linux__)
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const auto sent = send(ToRaw(handle), reinterpret_cast<const char*>(bytes), int(size), flags);
        if (sent >= 0)
            return long(sent);
        return WouldBlock() ? 0 : -1;
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
