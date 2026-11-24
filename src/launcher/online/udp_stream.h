#pragma once

#include "launcher/online/lobby_protocol.h"
#include <deque>
#include <functional>
#include <map>

namespace online
{
    // The callback also permits a deterministic datagram link without a socket.
    class UdpStream
    {
    public:
        using Send = std::function<void(std::span<const uint8_t>)>;
        static constexpr size_t DatagramBytes = 1200, HeaderBytes = 22,
                                PayloadBytes = DatagramBytes - HeaderBytes - TagBytes;
        static constexpr size_t Window = 64, MaxQueuedBytes = 70 * 1024 * 1024;
        static constexpr uint64_t MinRto = 100, MaxRto = 2000, SilenceMs = 15000;
        UdpStream(uint64_t now, CipherKey sendKey, CipherKey receiveKey, Send send);
        bool Write(std::span<const uint8_t> bytes);
        std::vector<uint8_t> Read();
        void Receive(std::span<const uint8_t> packet, uint64_t now);
        void Poll(uint64_t now);
        bool Closed() const { return m_closed; }
        bool Drained() const { return m_pending.empty() && m_sent.empty(); }
        size_t InFlight() const { return m_sent.size(); }
        uint64_t Rto() const { return m_rto; }
        uint64_t Retransmissions() const { return m_retransmissions; }

    private:
        struct Segment
        {
            uint32_t sequence;
            std::vector<uint8_t> bytes;
            uint64_t at, timeout;
            bool repeated = false, acked = false;
        };
        void Emit(uint32_t sequence, std::span<const uint8_t> bytes, uint64_t now);
        CipherKey m_sendKey, m_receiveKey;
        Send m_send;
        std::deque<uint8_t> m_pending;
        std::deque<Segment> m_sent;
        std::map<uint32_t, std::vector<uint8_t>> m_received;
        std::vector<uint8_t> m_ready;
        uint32_t m_nextSend = 1, m_nextReceive = 1;
        uint64_t m_lastHeard, m_lastSent, m_rto = 300, m_retransmissions = 0;
        double m_rtt = 0, m_variance = 0;
        bool m_closed = false, m_ack = false;
    };
}
