#include "launcher/online/udp_stream.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace online
{
    UdpStream::UdpStream(uint64_t now, CipherKey sendKey, CipherKey receiveKey, Send send)
        : m_sendKey(sendKey), m_receiveKey(receiveKey), m_send(std::move(send)), m_lastHeard(now), m_lastSent(now)
    {}

    bool UdpStream::Write(std::span<const uint8_t> bytes)
    {
        if (m_closed || bytes.size() > MaxQueuedBytes - m_pending.size())
            return false;
        m_pending.insert(m_pending.end(), bytes.begin(), bytes.end());
        return true;
    }

    std::vector<uint8_t> UdpStream::Read()
    {
        std::vector<uint8_t> ready;
        ready.swap(m_ready);
        return ready;
    }

    void UdpStream::Emit(uint32_t sequence, std::span<const uint8_t> bytes, uint64_t now)
    {
        Writer w;
        w.U32(0x33554b41);
        w.U32(sequence);
        w.U32(m_nextReceive - 1);
        uint64_t sack = 0;
        for (const auto& [seq, data] : m_received)
            sack |= uint64_t(1) << (seq - m_nextReceive);
        w.U64(sack);
        w.U16(uint16_t(bytes.size()));
        w.Raw(bytes);
        const auto tag = HmacSha256(m_sendKey, w.Bytes());
        w.Raw(std::span(tag).first(TagBytes));
        m_send(w.Bytes());
        m_ack = false;
        m_lastSent = now;
    }

    void UdpStream::Receive(std::span<const uint8_t> packet, uint64_t now)
    {
        if (m_closed || packet.size() < HeaderBytes + TagBytes || packet.size() > DatagramBytes)
            return;
        const auto body = packet.first(packet.size() - TagBytes);
        const auto tag = HmacSha256(m_receiveKey, body);
        uint8_t different = 0;
        for (size_t i = 0; i < TagBytes; ++i)
            different |= tag[i] ^ packet[body.size() + i];
        if (different)
            return;
        Reader r(body);
        if (r.U32() != 0x33554b41)
            return;
        const uint32_t seq = r.U32(), ack = r.U32();
        const uint64_t sack = r.U64();
        const size_t length = r.U16();
        if (!r.Ok() || body.size() != HeaderBytes + length || (seq == 0) != (length == 0) || ack >= m_nextSend)
            return;
        m_lastHeard = now;
        for (auto& segment : m_sent)
        {
            const uint64_t distance = uint64_t(segment.sequence) - ack;
            if (!segment.acked && (segment.sequence <= ack || (distance <= Window && ((sack >> (distance - 1)) & 1))))
            {
                segment.acked = true;
                // Retransmitted packets cannot identify which transmission was acknowledged.
                if (!segment.repeated)
                {
                    const double sample = double(now - segment.at);
                    if (m_rtt == 0)
                    {
                        m_rtt = sample;
                        m_variance = sample / 2;
                    }
                    else
                    {
                        m_variance = .75 * m_variance + .25 * std::abs(m_rtt - sample);
                        m_rtt = .875 * m_rtt + .125 * sample;
                    }
                    m_rto = std::clamp(uint64_t(std::ceil(m_rtt + 4 * m_variance)), MinRto, MaxRto);
                }
            }
        }
        while (!m_sent.empty() && m_sent.front().acked)
            m_sent.pop_front();
        if (seq == 0)
            return;
        m_ack = true;
        if (seq < m_nextReceive || uint64_t(seq) - m_nextReceive >= Window)
            return;
        m_received.try_emplace(seq, body.begin() + HeaderBytes, body.end());
        for (;;)
        {
            auto it = m_received.find(m_nextReceive);
            if (it == m_received.end())
                break;
            if (m_ready.size() + it->second.size() > MaxFrameBytes + Window * PayloadBytes ||
                m_nextReceive == std::numeric_limits<uint32_t>::max())
            {
                m_closed = true;
                return;
            }
            m_ready.insert(m_ready.end(), it->second.begin(), it->second.end());
            m_received.erase(it);
            ++m_nextReceive;
        }
    }

    void UdpStream::Poll(uint64_t now)
    {
        if (m_closed)
            return;
        if (now - m_lastHeard >= SilenceMs)
        {
            m_closed = true;
            return;
        }
        for (auto& segment : m_sent)
            if (!segment.acked && now - segment.at >= segment.timeout)
            {
                segment.at = now;
                segment.timeout = std::min(MaxRto, segment.timeout * 2);
                segment.repeated = true;
                ++m_retransmissions;
                Emit(segment.sequence, segment.bytes, now);
            }
        while (m_sent.size() < Window && !m_pending.empty())
        {
            if (m_nextSend == std::numeric_limits<uint32_t>::max())
            {
                m_closed = true;
                return;
            }
            const size_t length = std::min(PayloadBytes, m_pending.size());
            Segment segment{ m_nextSend++, {}, now, m_rto };
            segment.bytes.assign(m_pending.begin(), m_pending.begin() + ptrdiff_t(length));
            m_pending.erase(m_pending.begin(), m_pending.begin() + ptrdiff_t(length));
            m_sent.push_back(std::move(segment));
            Emit(m_sent.back().sequence, m_sent.back().bytes, now);
        }
        if (m_ack || now - m_lastSent >= 1000)
            Emit(0, {}, now);
    }
}
