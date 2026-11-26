#include "launcher/online/punch.h"
#include "launcher/online/lobby.h"
#include <algorithm>
#include <SDL3/SDL.h>

namespace online
{
    Punch::Punch(uint64_t secret, bool host, uint64_t now, Send send)
        : m_host(host), m_started(now), m_next(now), m_send(std::move(send))
    {
        Writer w;
        for (int i = 4; i >= 0; --i)
            w.U8(uint8_t(secret >> (8 * i)));
        constexpr std::string_view label = "arkeyan punch key";
        m_key = HmacSha256(w.Bytes(), { reinterpret_cast<const uint8_t*>(label.data()), label.size() });
        RandomBytes(m_nonce);
    }

    void Punch::Add(const std::vector<net::Endpoint>& candidates, uint64_t now)
    {
        const size_t before = m_candidates.size();
        for (const auto& endpoint : candidates)
            if (m_candidates.size() < 16 && endpoint.port && !net::Resolve(endpoint.ip, endpoint.port, true).empty() &&
                std::none_of(
                    m_candidates.begin(), m_candidates.end(), [&](const auto& c) { return c.first == endpoint; }))
                m_candidates.emplace_back(endpoint, now);
        if (before != m_candidates.size())
            SDL_Log("[online] punching: %zu candidates", m_candidates.size());
    }

    void Punch::Emit(const net::Endpoint& to, uint8_t type, const SessionKey& echoed)
    {
        Writer w;
        w.U32(0x33504b41);
        w.U8(type);
        w.U8(m_host ? 1 : 0);
        w.Raw(m_nonce);
        w.Raw(echoed);
        const auto tag = HmacSha256(m_key, w.Bytes());
        w.Raw(std::span(tag).first(TagBytes));
        m_send(to, w.Bytes());
    }

    void Punch::Poll(uint64_t now)
    {
        if (now < m_next)
            return;
        if (Connected())
        {
            if (now < m_confirmUntil)
                Emit(m_peer, 3, m_remote);
            m_next = now + EveryMs;
            return;
        }
        if (Expired(now))
            return;
        for (const auto& [endpoint, since] : m_candidates)
            if (now - since < TimeoutMs)
                Emit(endpoint, 1, {});
        m_next = now + EveryMs;
    }

    bool Punch::Receive(const net::Endpoint& from, std::span<const uint8_t> packet, uint64_t now, ByeReason refusal)
    {
        if (packet.size() != 54)
            return false;
        const auto body = packet.first(38);
        const auto tag = HmacSha256(m_key, body);
        uint8_t different = 0;
        for (size_t i = 0; i < TagBytes; ++i)
            different |= tag[i] ^ packet[38 + i];
        if (different)
            return false;
        Reader r(body);
        if (r.U32() != 0x33504b41)
            return false;
        const uint8_t type = r.U8(), role = r.U8();
        SessionKey nonce{}, echo{};
        r.Raw(nonce);
        r.Raw(echo);
        if (role > 1 || bool(role) == m_host || type < 1 || type > 5)
            return false;
        if (type == 1)
        {
            if (refusal == ByeReason::Full || refusal == ByeReason::Limited)
                Emit(from, refusal == ByeReason::Full ? 4 : 5, nonce);
            else if (!Connected() || from == m_peer)
            {
                Emit(from, 2, nonce);
                if (!m_host && !Connected())
                    Emit(from, 1, {});
            }
            return true;
        }
        if (refusal != ByeReason(0))
            return true;
        if (echo != m_nonce || (!m_host && now - m_started >= TimeoutMs))
            return false;
        if (type == 4 || type == 5)
        {
            if (!Connected())
                m_refused = type == 4 ? ByeReason::Full : ByeReason::Limited;
            return true;
        }
        if (Connected() && (from != m_peer || nonce != m_remote))
            return false;
        // The guest chooses one path so simultaneous IPv4 and IPv6 answers cannot select different peers.
        if ((m_host && type == 2) || (!m_host && type == 3 && !Connected()))
            return true;
        if (type == 2)
            Emit(from, 3, nonce);
        if (!Connected())
        {
            m_confirmUntil = now + TimeoutMs;
            m_remote = nonce;
            m_peer = from;
            SDL_Log("[online] punched through (%s)", from.ip.find(':') == std::string::npos ? "IPv4" : "IPv6");
        }
        return true;
    }

    CipherKey Punch::StreamKey(bool fromHost) const
    {
        Writer w;
        w.Text("arkeyan UDP stream");
        w.U8(fromHost ? 1 : 0);
        w.Raw(m_host ? m_nonce : m_remote);
        w.Raw(m_host ? m_remote : m_nonce);
        return HmacSha256(m_key, w.Bytes());
    }
}
