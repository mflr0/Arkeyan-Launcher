#include "launcher/online/udp_stream.h"
#include "launcher/online/punch.h"
#include "launcher/online/discovery.h"
#include "launcher/online/room_code.h"
#include "launcher/online/stun.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <queue>
#include <random>
#include <SDL3/SDL.h>

namespace
{
    int failures = 0;
    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            fprintf(stderr, "FAIL UDP: %s\n", message);
            ++failures;
        }
    }
    template <typename F>
    bool Wait(F done)
    {
        for (int i = 0; i < 2000; ++i)
        {
            if (done())
                return true;
            SDL_Delay(1);
        }
        return false;
    }
    void StreamChecks()
    {
        using namespace online;
        struct Datagram
        {
            uint64_t at;
            bool toHost;
            std::vector<uint8_t> bytes;
            bool operator<(const Datagram& b) const { return at > b.at; }
        };
        std::priority_queue<Datagram> link;
        std::mt19937 random(7351);
        uint64_t now = 0;
        CipherKey hostKey{}, guestKey{};
        hostKey[0] = 1;
        guestKey[0] = 2;
        size_t lost = 0, duplicated = 0, sent = 0;
        const auto send = [&](bool toHost, auto bytes) {
            ++sent;
            Expect(bytes.size() <= 1200, "maximum datagram size");
            if (random() % 5 == 0)
            {
                ++lost;
                return;
            }
            link.push({ now + 5 + random() % 150, toHost, { bytes.begin(), bytes.end() } });
            if (random() % 10 == 0)
            {
                ++duplicated;
                link.push({ now + 5 + random() % 150, toHost, { bytes.begin(), bytes.end() } });
            }
        };
        UdpStream host(now, hostKey, guestKey, [&](auto bytes) { send(false, bytes); });
        UdpStream guest(now, guestKey, hostKey, [&](auto bytes) { send(true, bytes); });
        std::vector<uint8_t> source(5 * 1024 * 1024), received, returned;
        for (auto& byte : source)
            byte = uint8_t(random());
        Expect(host.Write(source) && guest.Write(source), "queue two 5 MB streams");
        for (; now < 300000; now += 5)
        {
            while (!link.empty() && link.top().at <= now)
            {
                const auto packet = link.top();
                link.pop();
                (packet.toHost ? host : guest).Receive(packet.bytes, now);
            }
            host.Poll(now);
            guest.Poll(now);
            Expect(host.InFlight() <= 64 && guest.InFlight() <= 64, "bounded send window");
            const auto a = guest.Read(), b = host.Read();
            received.insert(received.end(), a.begin(), a.end());
            returned.insert(returned.end(), b.begin(), b.end());
            if (received.size() == source.size() && returned.size() == source.size() && host.Drained() &&
                guest.Drained())
                break;
            if (host.Closed() || guest.Closed())
                break;
        }
        Expect(received == source && returned == source,
            "5 MB in both directions survives loss, duplicates and reordering");
        Expect(lost > sent / 6 && duplicated > 100, "loss and duplication exercised");
        Expect(host.Retransmissions() > 0 && guest.Retransmissions() > 0, "lost segments retransmitted");
        Expect(host.Rto() >= 100 && host.Rto() <= 2000, "measured RTO bounded");

        std::vector<std::vector<uint8_t>> wire;
        UdpStream silent(0, hostKey, guestKey, [&](auto bytes) { wire.emplace_back(bytes.begin(), bytes.end()); });
        Expect(silent.Write(std::vector<uint8_t>(65 * UdpStream::PayloadBytes, 3)), "queue more than the window");
        silent.Poll(0);
        Expect(wire.size() == 64 && silent.InFlight() == 64, "initial window stops at 64");
        silent.Poll(299);
        Expect(silent.Retransmissions() == 0, "initial RTO waits 300 ms");
        silent.Poll(300);
        Expect(silent.Retransmissions() == 64, "RTO retransmits the window");
        silent.Poll(899);
        Expect(silent.Retransmissions() == 64, "RTO doubled after retry");
        silent.Poll(900);
        Expect(silent.Retransmissions() == 128, "second retry at doubled RTO");
        silent.Poll(2100);
        silent.Poll(4099);
        Expect(silent.Retransmissions() == 192, "backoff capped at two seconds");
        silent.Poll(4100);
        Expect(silent.Retransmissions() == 256, "capped retry fires");
        silent.Receive(wire.front(), 14999);
        silent.Poll(15000);
        Expect(silent.Closed(), "reflection does not postpone silence close");
        const size_t before = wire.size();
        silent.Poll(17000);
        Expect(wire.size() == before && !silent.Write(source), "closed stream sends and accepts nothing");

        wire.clear();
        UdpStream idle(0, hostKey, guestKey, [&](auto bytes) { wire.emplace_back(bytes.begin(), bytes.end()); });
        idle.Poll(999);
        Expect(wire.empty(), "keepalive waits one second");
        idle.Poll(1000);
        Expect(wire.size() == 1, "one-second keepalive");
        idle.Poll(14999);
        Expect(!idle.Closed(), "silence before deadline");
        idle.Poll(15000);
        Expect(idle.Closed(), "silence deadline closes");

        std::vector<std::vector<uint8_t>> data, acknowledgements;
        UdpStream a(0, hostKey, guestKey, [&](auto bytes) { data.emplace_back(bytes.begin(), bytes.end()); });
        UdpStream b(
            0, guestKey, hostKey, [&](auto bytes) { acknowledgements.emplace_back(bytes.begin(), bytes.end()); });
        a.Write(std::vector<uint8_t>(65 * UdpStream::PayloadBytes, 4));
        a.Poll(0);
        for (size_t i = 1; i < data.size(); ++i)
            b.Receive(data[i], 10);
        b.Poll(10);
        Expect(b.Read().empty(), "no delivery across a gap");
        a.Receive(acknowledgements.back(), 20);
        Expect(a.Rto() == 100, "RTT measurement reaches minimum RTO");
        a.Poll(300);
        Expect(a.InFlight() == 64 && a.Retransmissions() == 1,
            "SACK skips received segments and keeps the window anchored");
        auto corrupt = data.front();
        corrupt[22] ^= 1;
        b.Receive(corrupt, 301);
        Expect(b.Read().empty(), "altered datagram rejected before delivery");
        b.Receive(data.front(), 302);
        Expect(b.Read().size() == 64 * UdpStream::PayloadBytes, "gap closes in order");
    }

    void PunchChecks()
    {
        using namespace online;
        const uint64_t secret = 0x123456789a;
        const std::vector<net::Endpoint> hostEndpoints{ { "198.51.100.8", 41000 }, { "2001:db8::8", 41000 } };
        const std::vector<net::Endpoint> guestEndpoints{ { "203.0.113.7", 42000 } };
        struct Packet
        {
            bool toHost;
            net::Endpoint from, to;
            std::vector<uint8_t> bytes;
        };
        std::vector<Packet> link;
        std::vector<uint8_t> oldAnswer;
        bool hostSent = false, guestSent = false;
        Punch host(secret, true, 0, [&](const auto& to, auto bytes) {
            hostSent = true;
            link.push_back({ false, hostEndpoints[0], to, { bytes.begin(), bytes.end() } });
        });
        Punch guest(secret, false, 0, [&](const auto& to, auto bytes) {
            guestSent = true;
            link.push_back({ true, guestEndpoints[0], to, { bytes.begin(), bytes.end() } });
        });
        guest.Add({ { "192.0.2.1", 47640 } }, 0);
        for (uint64_t now = 0; now < 10000 && (!host.Connected() || !guest.Connected()); now += 50)
        {
            if (now == 500)
            {
                host.Add(guestEndpoints, now);
                guest.Add(hostEndpoints, now);
            }
            host.Poll(now);
            guest.Poll(now);
            auto packets = std::move(link);
            link.clear();
            std::reverse(packets.begin(), packets.end());
            for (const auto& packet : packets)
            {
                if (!hostSent || !guestSent ||
                    (packet.toHost ? packet.to != hostEndpoints[0] : packet.to != guestEndpoints[0]))
                    continue;
                auto& receiver = packet.toHost ? host : guest;
                receiver.Receive(packet.from, packet.bytes, now);
                receiver.Receive(packet.from, packet.bytes, now);
                if (!packet.toHost && packet.bytes[4] == 2)
                    oldAnswer = packet.bytes;
            }
        }
        Expect(host.Connected() && guest.Connected(),
            "simultaneous NAT punches reach a candidate after the direct path fails");
        Expect(
            host.Peer() == guestEndpoints[0] && guest.Peer() == hostEndpoints[0], "reached public endpoints retained");
        Expect(host.StreamKey(true) == guest.StreamKey(true) && host.StreamKey(false) == guest.StreamKey(false) &&
                   host.StreamKey(true) != host.StreamKey(false),
            "fresh directional stream keys agree");
        Punch fresh(secret, false, 0, [](const auto&, auto) {});
        Expect(!fresh.Receive(hostEndpoints[0], oldAnswer, 1) && !fresh.Connected(),
            "earlier punch answer cannot select a new peer");
        Punch wrong(secret + 1, false, 0, [](const auto&, auto) {});
        Expect(!wrong.Receive(hostEndpoints[0], oldAnswer, 1), "wrong secret rejected");
        Expect(!fresh.Expired(9999) && fresh.Expired(10000), "punch deadline is ten seconds");
        Expect(!fresh.Receive(hostEndpoints[0], oldAnswer, 10001), "late punch rejected");
    }

    void ConfirmChecks()
    {
        using namespace online;
        struct Packet
        {
            bool toHost;
            std::vector<uint8_t> bytes;
        };
        std::vector<Packet> wire;
        bool dropped = false;
        const net::Endpoint hostEndpoint{ "192.0.2.1", 47640 }, guestEndpoint{ "198.51.100.1", 50001 };
        Punch host(
            42, true, 0, [&](const auto&, auto bytes) { wire.push_back({ false, { bytes.begin(), bytes.end() } }); });
        Punch guest(42, false, 0, [&](const auto&, auto bytes) {
            if (bytes[4] == 3 && !dropped)
            {
                dropped = true;
                return;
            }
            wire.push_back({ true, { bytes.begin(), bytes.end() } });
        });
        guest.Add({ hostEndpoint }, 0);
        for (uint64_t now = 0; now < 1000 && !host.Connected(); now += 10)
        {
            guest.Poll(now);
            host.Poll(now);
            auto packets = std::move(wire);
            wire.clear();
            for (const auto& packet : packets)
                (packet.toHost ? host : guest).Receive(packet.toHost ? guestEndpoint : hostEndpoint, packet.bytes, now);
        }
        Expect(dropped && host.Connected() && guest.Connected(), "lost punch confirmation is retried");
        Expect(host.StreamKey(true) == guest.StreamKey(true), "retry keeps the same stream key");
    }

    void DiscoveryChecks()
    {
        using namespace online;
        const auto caller = std::this_thread::get_id();
        std::atomic<bool> resolvedOffThread = false;
        Discovery resolver([&](const std::string& host, uint16_t port) {
            resolvedOffThread = std::this_thread::get_id() != caller;
            return host == "tunnel.invalid" ? std::vector<net::Endpoint>{ { "2001:db8::42", port } }
                                            : std::vector<net::Endpoint>{};
        });
        resolver.ResolveTunnel("tunnel.invalid:55000", 47640);
        Expect(Wait([&] { return !resolver.Tunnel().ip.empty(); }) &&
                   resolver.Tunnel() == net::Endpoint{ "2001:db8::42", 55000 },
            "tunnel hostname becomes an endpoint on the worker");
        Expect(resolvedOffThread, "name resolution leaves the calling thread free");
        resolver.ResolveTunnel("tunnel.invalid", 47641);
        Expect(Wait([&] { return !resolver.Tunnel().ip.empty(); }) && resolver.Tunnel().port == 47641,
            "tunnel endpoint follows changes to the room port");
        resolver.ResolveTunnel("", 47641);
        Expect(resolver.Tunnel().ip.empty(), "clearing the address clears the old endpoint");

        std::atomic<unsigned> resolutions = 0;
        Discovery discovery([&](const std::string&, uint16_t port) {
            ++resolutions;
            return std::vector<net::Endpoint>{ { "192.0.2.1", port } };
        });
        discovery.Start(false);
        discovery.ResolveTunnel("tunnel.invalid", 47640);
        Expect(
            Wait([&] { return !discovery.Tunnel().ip.empty(); }) && resolutions == 1 && discovery.StunServers().empty(),
            "disabled STUN discovery still resolves a tunnel address");
        discovery.Start();
        Expect(Wait([&] { return discovery.StunServers().size() == stun::Servers.size(); }),
            "STUN server names resolve on the worker without sending requests");
        discovery.Close();
        Expect(discovery.StunServers().empty(), "closing clears STUN servers");

        std::mutex gateMutex;
        std::condition_variable gate;
        bool entered = false, release = false;
        Discovery closing([&](const std::string&, uint16_t port) {
            std::unique_lock lock(gateMutex);
            entered = true;
            gate.notify_one();
            gate.wait(lock, [&] { return release; });
            return std::vector<net::Endpoint>{ { "192.0.2.2", port } };
        });
        closing.Start();
        {
            std::unique_lock lock(gateMutex);
            Expect(gate.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }),
                "STUN resolution starts off the calling thread");
        }
        closing.Close();
        closing.ResolveTunnel("tunnel.invalid", 47640);
        {
            std::lock_guard lock(gateMutex);
            release = true;
        }
        gate.notify_one();
        Expect(Wait([&] { return !closing.Tunnel().ip.empty(); }) && closing.StunServers().empty(),
            "a late STUN lookup cannot restore servers after closing");
    }
}

int UdpChecks()
{
    failures = 0;
    StreamChecks();
    PunchChecks();
    ConfirmChecks();
    DiscoveryChecks();
    if (!failures)
        printf("UDP, punching and discovery checks: all passed (no network)\n");
    return failures;
}
