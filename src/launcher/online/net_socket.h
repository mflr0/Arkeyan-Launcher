#pragma once

// Non-blocking TCP for the online lobby (online/lobby.h): a listener on every address of this computer (IPv6 with IPv4
// mapped where the system allows it), and a connection to the address a player pasted. BSD sockets on macOS and Linux,
// Winsock on Windows. Nothing here waits: the lobby polls every frame of the window.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace online::net
{
    using Handle = intptr_t;
    constexpr Handle Invalid = -1;

    bool Startup();
    // Listens on `port` (TCP). Invalid with `error` (the system's words) when the port is taken or refused.
    Handle Listen(uint16_t port, std::string& error);
    // The next connection waiting, with the peer's address for the log. Invalid when none waits.
    Handle Accept(Handle listener, std::string& peer);
    // Starts connecting to `host` (a name, IPv4 or IPv6), `port`. Invalid with `error` when the name does not resolve.
    Handle Connect(const std::string& host, uint16_t port, std::string& error);
    enum class Progress
    {
        Pending,
        Connected,
        Failed,
    };
    Progress Connecting(Handle handle);
    // > 0: bytes read; 0: the peer closed; -1: nothing to read now; -2: the connection broke.
    long Receive(Handle handle, uint8_t* buffer, size_t capacity);
    // Bytes taken (0: the system's buffer is full, try again), -1: the connection broke.
    long Send(Handle handle, const uint8_t* bytes, size_t size);
    void Close(Handle handle);

    // "1.2.3.4:5", "[2001:db8::1]:5", "name:5", or an address alone (then `defaultPort`). False on a malformed text.
    bool ParseAddress(std::string_view text, uint16_t defaultPort, std::string& host, uint16_t& port);
}
