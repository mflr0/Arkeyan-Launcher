#pragma once

#include "launcher/online/net_socket.h"
#include "launcher/online/lobby_protocol.h"
#include <functional>

namespace online
{
    class Punch
    {
    public:
        using Send = std::function<void(const net::Endpoint&, std::span<const uint8_t>)>;
        static constexpr uint64_t EveryMs = 100, TimeoutMs = 10000;
        Punch(uint64_t secret, bool host, uint64_t now, Send send);
        void Add(const std::vector<net::Endpoint>& candidates, uint64_t now);
        void Poll(uint64_t now);
        bool Receive(
            const net::Endpoint& from, std::span<const uint8_t> packet, uint64_t now, ByeReason refusal = ByeReason(0));
        bool Connected() const { return !m_peer.ip.empty(); }
        bool Expired(uint64_t now) const { return !m_host && !Connected() && now - m_started >= TimeoutMs; }
        ByeReason Refused() const { return m_refused; }
        const net::Endpoint& Peer() const { return m_peer; }
        CipherKey StreamKey(bool fromHost) const;

    private:
        using SessionKey = std::array<uint8_t, 16>;
        void Emit(const net::Endpoint& to, uint8_t type, const SessionKey& echoed);
        using Candidate = std::pair<net::Endpoint, uint64_t>;
        // Kept separate from the lobby nonces so reconnects cannot accept an earlier stream.
        CipherKey m_key;
        SessionKey m_nonce{}, m_remote{};
        bool m_host;
        uint64_t m_started, m_next, m_confirmUntil = 0;
        Send m_send;
        std::vector<Candidate> m_candidates;
        net::Endpoint m_peer;
        ByeReason m_refused = ByeReason(0);
    };
}
