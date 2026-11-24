#pragma once

// Non-blocking datagrams shared by the room stream, punching and STUN.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace online::net
{
    using Handle = intptr_t;
    constexpr Handle Invalid = -1;

    bool Startup();
    struct Endpoint
    {
        std::string ip;
        uint16_t port = 0;
        bool operator==(const Endpoint&) const = default;
    };
    Handle BindUdp(uint16_t port, std::string& error);
    uint16_t LocalPort(Handle handle);
    // Names may block: callers resolve them on a worker; numeric addresses never use DNS.
    std::vector<Endpoint> Resolve(const std::string& host, uint16_t port, bool numeric = false);
    std::string Address(const Endpoint& endpoint);
    // -1: nothing waiting; -2: socket error.
    long ReceiveFrom(Handle handle, uint8_t* bytes, size_t capacity, Endpoint& peer);
    long SendTo(Handle handle, std::span<const uint8_t> bytes, const Endpoint& peer);
    void Close(Handle handle);

    // The room can substitute a deterministic datagram link in its in-process tests.
    class DatagramIo
    {
    public:
        virtual ~DatagramIo() = default;
        virtual Handle Bind(uint16_t port, std::string& error) { return BindUdp(port, error); }
        virtual uint16_t Port(Handle handle) { return LocalPort(handle); }
        virtual long Receive(Handle handle, uint8_t* bytes, size_t size, Endpoint& from)
        {
            return ReceiveFrom(handle, bytes, size, from);
        }
        virtual long Send(Handle handle, std::span<const uint8_t> bytes, const Endpoint& to)
        {
            return SendTo(handle, bytes, to);
        }
        virtual void Release(Handle handle) { Close(handle); }
    };

    // "1.2.3.4:5", "[2001:db8::1]:5", "name:5", or an address alone (then `defaultPort`). False on a malformed text.
    bool ParseAddress(std::string_view text, uint16_t defaultPort, std::string& host, uint16_t& port);
}
