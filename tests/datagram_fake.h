#pragma once

#include "launcher/online/net_socket.h"
#include <algorithm>
#include <deque>
#include <map>

namespace online_tests
{
    class DatagramFake : public online::net::DatagramIo
    {
    public:
        using Handle = online::net::Handle;
        using Endpoint = online::net::Endpoint;
        bool impaired = false;
        Handle Bind(uint16_t port, std::string& error) override
        {
            if (!port)
                port = ++m_ephemeral;
            for (const auto& [id, socket] : m_sockets)
                if (socket.port == port)
                {
                    error = "port in use";
                    return online::net::Invalid;
                }
            const Handle handle = ++m_next;
            m_sockets.emplace(handle, Socket{ port, {} });
            return handle;
        }
        uint16_t Port(Handle handle) override { return m_sockets.at(handle).port; }
        long Receive(Handle handle, uint8_t* bytes, size_t size, Endpoint& from) override
        {
            auto& packets = m_sockets.at(handle).packets;
            if (packets.empty())
                return -1;
            auto packet = std::move(packets.front());
            packets.pop_front();
            if (packet.bytes.size() > size)
                return -2;
            std::copy(packet.bytes.begin(), packet.bytes.end(), bytes);
            from = packet.from;
            return long(packet.bytes.size());
        }
        long Send(Handle handle, std::span<const uint8_t> bytes, const Endpoint& to) override
        {
            for (auto& [id, socket] : m_sockets)
                if (socket.port == to.port && to.ip == "127.0.0.1")
                {
                    if (impaired && Random() % 5 == 0)
                        continue;
                    Packet packet{ { "127.0.0.1", Port(handle) }, { bytes.begin(), bytes.end() } };
                    if (impaired && Random() % 2 == 0)
                        socket.packets.push_front(packet);
                    else
                        socket.packets.push_back(packet);
                    if (impaired && Random() % 10 == 0)
                        socket.packets.push_front(packet);
                }
            return long(bytes.size());
        }
        void Release(Handle handle) override { m_sockets.erase(handle); }

    private:
        uint32_t Random()
        {
            m_random ^= m_random << 13;
            m_random ^= m_random >> 17;
            m_random ^= m_random << 5;
            return m_random;
        }
        uint32_t m_random = 97653;
        struct Packet
        {
            Endpoint from;
            std::vector<uint8_t> bytes;
        };
        struct Socket
        {
            uint16_t port;
            std::deque<Packet> packets;
        };
        std::map<Handle, Socket> m_sockets;
        Handle m_next = 0;
        uint16_t m_ephemeral = 50000;
    };
}
