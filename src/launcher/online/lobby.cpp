#include "launcher/online/lobby.h"
#include "launcher/online/room_code.h"
#include <limits>
#include <SDL3/SDL.h>
#include <algorithm>
#include <random>

namespace online
{
    void RandomBytes(std::span<uint8_t> out)
    {
        std::random_device device;
        for (auto& byte : out)
            byte = uint8_t(device());
    }

    bool Status::CanStart() const { return host && state == State::InRoom && localReady && remoteReady && haveRemote; }

    Lobby::Lobby(Clock clock, std::shared_ptr<net::DatagramIo> io)
        : m_clock(std::move(clock)), m_io(io ? std::move(io) : std::make_shared<net::DatagramIo>())
    {}

    Lobby::~Lobby() { Leave(); }

    uint64_t Lobby::Now() const { return m_clock ? m_clock() : SDL_GetTicks(); }

    bool Lobby::Host(HostOptions options)
    {
        Leave();
        m_status = {};
        m_status.host = true;
        m_wrongProofs.clear();
        m_refuseUntil = 0;
        m_secret = options.secret & 0xFFFFFFFFFFull;
        while (m_secret == 0)
        {
            std::array<uint8_t, 5> secret{};
            RandomBytes(secret);
            for (const uint8_t byte : secret)
                m_secret = (m_secret << 8) | byte;
        }
        options.hello.host = true;
        RandomBytes(options.hello.nonce);
        options.settings.saveFiles = uint16_t(options.save.size());
        options.settings.saveBytes = 0;
        for (const SaveFile& file : options.save)
            options.settings.saveBytes += uint32_t(file.bytes.size());
        std::string error;
        m_socket = m_io->Bind(options.port, error);
        if (m_socket == net::Invalid)
        {
            m_status.state = State::Ended;
            m_status.error = Error::PortInUse;
            SDL_Log("[online] cannot open port %u: %s", options.port, error.c_str());
            return false;
        }
        m_mine = options.hello;
        m_player = options.player;
        m_hostOptions = std::move(options);
        m_status.state = State::Waiting;
        m_status.localPort = m_io->Port(m_socket);
        m_discovery.Start(m_hostOptions.discover);
        m_punch = std::make_unique<Punch>(
            m_secret, true, Now(), [this](const auto& to, auto bytes) { m_io->Send(m_socket, bytes, to); });
        SDL_Log("[online] room open on port %u", m_hostOptions.port);
        return true;
    }

    bool Lobby::Join(JoinOptions options)
    {
        Leave();
        m_status = {};
        options.hello.host = false;
        RandomBytes(options.hello.nonce);
        std::string host, error;
        uint16_t port = 0;
        std::vector<net::Endpoint> endpoints;
        if (!DecodeRoomCode(options.address, endpoints, m_secret))
        {
            End(net::ParseAddress(options.address, DefaultPort, host, port) ? Error::CodeRequired : Error::BadAddress);
            return false;
        }
        m_socket = m_io->Bind(0, error);
        if (m_socket == net::Invalid)
        {
            SDL_Log("[online] cannot reach the room: %s", error.c_str());
            End(Error::CannotConnect);
            return false;
        }
        m_mine = options.hello;
        m_player = options.player;
        m_joinOptions = std::move(options);
        m_status.peer = endpoints.front().ip;
        m_status.localPort = m_io->Port(m_socket);
        m_discovery.Start(m_joinOptions.discover);
        m_punch = std::make_unique<Punch>(
            m_secret, false, Now(), [this](const auto& to, auto bytes) { m_io->Send(m_socket, bytes, to); });
        m_punch->Add(endpoints, Now());
        m_status.state = State::Connecting;
        m_startedAt = Now();
        return true;
    }

    void Lobby::Send(MessageType type, const std::vector<uint8_t>& payload)
    {
        // Close before the sequence can wrap and reuse a cipher nonce.
        if (m_sendSequence == std::numeric_limits<uint32_t>::max())
        {
            Fail(Error::Broken);
            return;
        }
        const auto frame = Frame(type, m_sendSequence++, payload, m_haveKeys ? &m_keys : nullptr, m_status.host);
        m_out.insert(m_out.end(), frame.begin(), frame.end());
    }

    void Lobby::SendBye(ByeReason reason) { Send(MessageType::Bye, { uint8_t(reason) }); }

    void Lobby::Flush()
    {
        if (!m_stream)
            return;
        if (m_outStart < m_out.size())
        {
            if (!m_stream->Write(std::span(m_out).subspan(m_outStart)))
            {
                Fail(Error::Broken);
                return;
            }
            m_out.clear();
            m_outStart = 0;
        }
        m_stream->Poll(Now());
    }

    void Lobby::CloseConnection()
    {
        m_stream.reset();
        m_connected = false;
        m_reader = {};
        m_out.clear();
        m_outStart = 0;
        m_haveKeys = false;
        m_sendSequence = m_receiveSequence = 0;
        m_step = Step::AwaitHello;
        m_startQueued = false;
        m_guestStartAt = 0;
        m_failure.reset();
    }

    const char* ErrorName(Error error)
    {
        switch (error)
        {
        case Error::None: return "none";
        case Error::PortInUse: return "the port is in use";
        case Error::BadAddress: return "not a room code or an address";
        case Error::CannotConnect: return "nothing answered";
        case Error::Timeout: return "the other side stopped answering";
        case Error::Mismatch: return "the two sides do not match";
        case Error::WrongCode: return "wrong room code";
        case Error::CodeRequired: return "a room code is required";
        case Error::TooManyCodes: return "too many wrong codes: guests refused for 30 s";
        case Error::RoomFull: return "the room is full";
        case Error::Left: return "the other player left";
        case Error::Broken: return "unexpected or malformed messages";
        }
        return "unknown";
    }

    std::string Lobby::MismatchDetail(Mismatch mismatch) const
    {
        const Hello& mine = m_mine;
        const Hello& theirs = m_status.remoteHello;
        const auto both = [](const char* what, const std::string& a, const std::string& b) {
            return std::string(" (") + what + ": " + a + " here, " + b + " there)";
        };
        switch (mismatch)
        {
        case Mismatch::None: return "";
        case Mismatch::Protocol:
            return both("room protocol", std::to_string(mine.protocol), std::to_string(theirs.protocol));
        case Mismatch::Launcher: return both("launcher", mine.launcherVersion, theirs.launcherVersion);
        case Mismatch::Game: return both("game", mine.gameId, theirs.gameId);
        case Mismatch::Disc: return " (disc editions differ)";
        case Mismatch::GameVersion: return both("game version", mine.gameVersion, theirs.gameVersion);
        case Mismatch::Platform: return both("system", mine.platform, theirs.platform);
        }
        return "";
    }

    void Lobby::FinishTest()
    {
        m_status.testDone = true;
        m_status.testSamples = uint32_t(m_testSamples.size());
        if (m_testSamples.empty())
            return;
        std::vector<double> sorted = m_testSamples;
        std::sort(sorted.begin(), sorted.end());
        m_status.testRttMs = sorted[sorted.size() / 2];
        m_status.testJitterMs = sorted[std::min(sorted.size() - 1, sorted.size() * 9 / 10)] - m_status.testRttMs;
        SDL_Log("[online] connection test: %zu round trips, median %.0f ms, jitter %.0f ms", sorted.size(),
            m_status.testRttMs, m_status.testJitterMs);
    }

    void Lobby::End(Error error, Mismatch mismatch)
    {
        if (error != Error::None)
            SDL_Log("[online] room ended: %s%s", ErrorName(error), MismatchDetail(mismatch).c_str());
        CloseConnection();
        m_io->Release(m_socket);
        m_socket = net::Invalid;
        m_punch.reset();
        m_discovery.Close();
        m_status.error = error;
        m_status.mismatch = mismatch;
        if (m_status.state != State::Started)
            m_status.state = State::Ended;
    }

    // The host's room stays open for the next guest; what this one got wrong is kept for the room's notice.
    void Lobby::DropGuest(Error error, Mismatch mismatch)
    {
        SDL_Log("[online] a guest was turned away, the room stays open: %s%s", ErrorName(error),
            MismatchDetail(mismatch).c_str());
        m_status.lastRefusal = { error, mismatch, m_status.peer, Now() };
        CloseConnection();
        const Refusal refusal = m_status.lastRefusal;
        const auto port = m_status.localPort;
        const auto mapped = m_status.mapped;
        m_status = {};
        m_status.host = true;
        m_status.localPort = port;
        m_status.mapped = mapped;
        m_status.state = State::Waiting;
        m_status.lastRefusal = refusal;
        m_punch = std::make_unique<Punch>(
            m_secret, true, Now(), [this](const auto& to, auto bytes) { m_io->Send(m_socket, bytes, to); });
        m_discovery.Start(m_hostOptions.discover);
    }

    void Lobby::Fail(Error error, Mismatch mismatch)
    {
        if (!m_status.host || m_status.state == State::Started)
        {
            End(error, mismatch);
            return;
        }
        if (!m_stream || error == Error::Timeout || error == Error::Left || error == Error::None)
        {
            DropGuest(error, mismatch);
            return;
        }
        if (!m_failure)
        {
            m_failure = { error, mismatch };
            const ByeReason reason = error == Error::WrongCode      ? ByeReason::Code
                                     : error == Error::TooManyCodes ? ByeReason::Limited
                                     : error == Error::Mismatch     ? ByeReason::Mismatch
                                                                    : ByeReason::Protocol;
            SendBye(reason);
        }
    }

    void Lobby::Leave()
    {
        if (m_stream && m_connected && m_status.state != State::Started)
        {
            SendBye(ByeReason::Left);
            Flush();
        }
        CloseConnection();
        m_io->Release(m_socket);
        m_socket = net::Invalid;
        m_punch.reset();
        m_discovery.Close();
        m_probes.clear();
        if (m_status.state != State::Started && m_status.state != State::Idle)
            m_status.state = State::Ended;
    }

    void Lobby::UpdatePlayer(const Player& player)
    {
        if (m_startQueued || m_guestStartAt)
            return;
        m_player = player;
        m_status.localReady = false;
        if (m_status.state == State::InRoom)
        {
            Send(MessageType::Player, Encode(m_player));
            Send(MessageType::Ready, { 0 });
        }
    }

    void Lobby::SetReady(bool ready)
    {
        if (m_startQueued || m_guestStartAt || m_status.state != State::InRoom || m_status.localReady == ready)
            return;
        m_status.localReady = ready;
        SDL_Log("[online] you are %s", ready ? "ready" : "no longer ready");
        Send(MessageType::Ready, { uint8_t(ready ? 1 : 0) });
    }

    bool Lobby::StartGame()
    {
        if (!m_status.CanStart() || m_startQueued)
            return false;
        Start start;
        RandomBytes(start.sessionKey);
        const double frameMs = 1000.0 / m_hostOptions.settings.fpsLimit;
        start.inputDelay = m_status.rttMs >= 0 ? InputDelayFor(m_status.rttMs, frameMs) : 2;
        start.port = m_hostOptions.port;
        Send(MessageType::Start, Encode(start));
        Flush();
        m_status.start = start;
        m_status.inputDelay = start.inputDelay;
        m_startQueued = true;
        SDL_Log("[online] starting: input delay %u frames, round trip %.0f ms, %u FPS, save %u bytes in %u files",
            start.inputDelay, m_status.rttMs, m_hostOptions.settings.fpsLimit, m_hostOptions.settings.saveBytes,
            unsigned(m_hostOptions.settings.saveFiles));
        return true;
    }

    void Lobby::SendRoomOpening()
    {
        Send(MessageType::Player, Encode(m_player));
        if (!m_status.host)
            return;
        Send(MessageType::Settings, Encode(m_hostOptions.settings));
        for (const SaveFile& file : m_hostOptions.save)
        {
            size_t offset = 0;
            do
            {
                SaveChunk chunk;
                chunk.file = file.path;
                chunk.fileBytes = uint32_t(file.bytes.size());
                chunk.offset = uint32_t(offset);
                const size_t size = std::min(SaveChunkBytes, file.bytes.size() - offset);
                chunk.bytes.assign(
                    file.bytes.begin() + ptrdiff_t(offset), file.bytes.begin() + ptrdiff_t(offset + size));
                Send(MessageType::SaveChunk, Encode(chunk));
                offset += size;
            } while (offset < file.bytes.size());
        }
    }

    void Lobby::HandleHello(const Received& frame)
    {
        if (!m_status.host && frame.type == MessageType::Bye && frame.payload.size() == 1 &&
            (ByeReason(frame.payload[0]) == ByeReason::Full || ByeReason(frame.payload[0]) == ByeReason::Limited))
        {
            End(ByeReason(frame.payload[0]) == ByeReason::Full ? Error::RoomFull : Error::TooManyCodes);
            return;
        }
        Hello theirs;
        if (frame.type != MessageType::Hello || !Decode(frame.payload, theirs) || theirs.host == m_status.host)
        {
            Fail(Error::Broken);
            return;
        }
        m_status.remoteHello = theirs;
        SDL_Log("[online] the other player: launcher %s, game %s %s, %s", theirs.launcherVersion.c_str(),
            theirs.gameId.c_str(), theirs.gameVersion.c_str(), theirs.platform.c_str());
        if (m_status.host)
            Send(MessageType::Hello, Encode(m_mine)); // the guest can then say exactly what differs
        const Mismatch mismatch = Compare(m_mine, theirs);
        if (mismatch != Mismatch::None)
        {
            if (m_status.host)
            {
                Fail(Error::Mismatch, mismatch);
            }
            else
                End(Error::Mismatch, mismatch);
            return;
        }
        const Nonce& hostNonce = m_status.host ? m_mine.nonce : theirs.nonce;
        const Nonce& guestNonce = m_status.host ? theirs.nonce : m_mine.nonce;
        m_keys = DeriveKeys(m_secret, hostNonce, guestNonce);
        m_haveKeys = true;
        m_step = Step::AwaitProof;
        // The guest proves first; the host answers only to a guest that knows the code.
        if (!m_status.host)
        {
            const Tag proof = ProofOf(m_keys, false);
            Send(MessageType::Proof, std::vector<uint8_t>(proof.begin(), proof.end()));
        }
    }

    void Lobby::HandleProof(const Received& frame)
    {
        const bool fromHost = !m_status.host;
        const Tag expected = ProofOf(m_keys, fromHost);
        uint8_t difference = 0;
        if (frame.payload.size() == expected.size())
            for (size_t i = 0; i < expected.size(); ++i)
                difference |= uint8_t(expected[i] ^ frame.payload[i]);
        else
            difference = 1;
        if (frame.type != MessageType::Proof || difference != 0)
        {
            WrongProof();
            return;
        }
        if (m_status.host)
        {
            const Tag proof = ProofOf(m_keys, true);
            Send(MessageType::Proof, std::vector<uint8_t>(proof.begin(), proof.end()));
        }
        m_step = Step::Room;
        m_status.state = State::InRoom;
        m_nextPing = Now();
        m_testStart = Now();
        m_testSamples.clear();
        SDL_Log("[online] %s in the room", m_status.host ? "guest" : "host");
        SendRoomOpening();
    }

    void Lobby::HandleRoom(const Received& frame)
    {
        bool ok = true;
        switch (frame.type)
        {
        case MessageType::Player:
        {
            Player player;
            ok = Decode(frame.payload, player);
            if (ok)
            {
                m_status.remote = std::move(player);
                m_status.haveRemote = true;
            }
            break;
        }
        case MessageType::Settings:
            ok = !m_status.host && !m_status.haveSettings && Decode(frame.payload, m_status.settings);
            m_status.haveSettings = ok;
            break;
        case MessageType::SaveChunk:
        {
            SaveChunk chunk;
            ok = !m_status.host && m_status.haveSettings && Decode(frame.payload, chunk) &&
                 m_status.saveReceived + chunk.bytes.size() <= m_status.settings.saveBytes;
            if (!ok)
                break;
            if (chunk.offset == 0)
            {
                ok = m_status.save.size() < m_status.settings.saveFiles;
                if (ok)
                    m_status.save.push_back({ chunk.file, {} });
            }
            else
                ok = !m_status.save.empty() && m_status.save.back().path == chunk.file &&
                     m_status.save.back().bytes.size() == chunk.offset;
            if (ok)
            {
                auto& bytes = m_status.save.back().bytes;
                bytes.insert(bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
                m_status.saveReceived += uint32_t(chunk.bytes.size());
            }
            break;
        }
        case MessageType::Ping:
            ok = frame.payload.size() == 8;
            if (ok)
                Send(MessageType::Pong, frame.payload);
            break;
        case MessageType::Pong:
        {
            Reader r(frame.payload);
            const uint64_t stamp = r.U64();
            ok = r.AtEnd() && stamp <= Now();
            if (ok)
            {
                const double sample = double(Now() - stamp);
                m_status.rttMs = m_status.rttMs < 0 ? sample : 0.75 * m_status.rttMs + 0.25 * sample;
                if (!m_status.testDone && m_testSamples.size() < 1000)
                    m_testSamples.push_back(sample);
                const uint8_t fps = m_status.host ? m_hostOptions.settings.fpsLimit
                                                  : (m_status.haveSettings ? m_status.settings.fpsLimit : 30);
                m_status.inputDelay = InputDelayFor(m_status.rttMs, 1000.0 / fps);
                if (Now() >= m_nextRttLog)
                {
                    SDL_Log("[online] round trip %.0f ms (last %.0f ms), input delay %u frames", m_status.rttMs, sample,
                        m_status.inputDelay);
                    m_nextRttLog = Now() + RttLogEveryMs;
                }
            }
            break;
        }
        case MessageType::Ready:
            ok = frame.payload.size() == 1 && frame.payload[0] <= 1;
            if (ok && m_status.remoteReady != (frame.payload[0] == 1))
            {
                m_status.remoteReady = frame.payload[0] == 1;
                SDL_Log("[online] the other player is %s", m_status.remoteReady ? "ready" : "no longer ready");
            }
            break;
        case MessageType::Start:
        {
            Start start;
            const bool wholeSave = m_status.haveSettings && m_status.saveReceived == m_status.settings.saveBytes &&
                                   m_status.save.size() == m_status.settings.saveFiles;
            ok = !m_status.host && wholeSave && Decode(frame.payload, start);
            if (ok)
            {
                m_status.start = start;
                m_status.inputDelay = start.inputDelay;
                m_guestStartAt = Now() + UdpStream::MaxRto;
                SDL_Log("[online] the host starts: input delay %u frames, round trip %.0f ms", start.inputDelay,
                    m_status.rttMs);
                return;
            }
            break;
        }
        case MessageType::Bye: Fail(Error::Left); return;
        default: ok = false;
        }
        if (!ok)
        {
            SendBye(ByeReason::Protocol);
            Flush();
            Fail(Error::Broken);
        }
    }

    void Lobby::Handle(Received frame)
    {
        if (m_receiveSequence == std::numeric_limits<uint32_t>::max() || frame.sequence != m_receiveSequence++)
        {
            Fail(Error::Broken);
            return;
        }
        if (m_step != Step::AwaitHello && !OpenFrame(frame, m_keys, !m_status.host))
        {
            if (m_step == Step::AwaitProof)
                WrongProof();
            else
                Fail(Error::Broken);
            return;
        }
        switch (m_step)
        {
        case Step::AwaitHello:
            // The host may answer a guest it refuses with an unauthenticated Bye right after its hello.
            HandleHello(frame);
            break;
        case Step::AwaitProof: HandleProof(frame); break;
        case Step::Room: HandleRoom(frame); break;
        }
    }

    void Lobby::WrongProof()
    {
        if (m_status.host)
        {
            const uint64_t now = Now();
            std::erase_if(m_wrongProofs, [&](uint64_t at) { return now - at >= 60000; });
            m_wrongProofs.push_back(now);
            if (m_wrongProofs.size() >= 5)
            {
                m_refuseUntil = now + 30000;
                m_wrongProofs.clear();
                SDL_Log("[online] too many wrong codes: guests refused for 30 s");
                Fail(Error::TooManyCodes);
                return;
            }
        }
        Fail(Error::WrongCode);
    }

    bool Lobby::ReadAll()
    {
        if (!m_stream)
            return true;
        const auto bytes = m_stream->Read();
        m_reader.Feed(bytes);
        while (m_stream && !m_failure)
        {
            auto frame = m_reader.Next();
            if (!frame)
                break;
            Handle(*frame);
        }
        if (!m_failure && m_reader.Broken())
            Fail(Error::Broken);
        return true;
    }

    void Lobby::Discover(uint64_t now)
    {
        if (!m_punch)
            return;
        const auto servers = m_discovery.StunServers();
        if (m_probes.empty() && !servers.empty())
            for (const auto& server : servers)
            {
                Probe probe{};
                probe.server = server;
                RandomBytes(probe.id);
                m_probes.push_back(probe);
            }
        for (auto& probe : m_probes)
        {
            if (probe.attempts >= 3 && now >= probe.next + 20000)
            {
                probe.attempts = 0;
                RandomBytes(probe.id);
            }
            if (probe.attempts < 3 && now >= probe.next)
            {
                m_io->Send(m_socket, stun::BindingRequest(probe.id), probe.server);
                probe.next = now + 500;
                ++probe.attempts;
            }
        }
    }

    void Lobby::Connected(uint64_t now)
    {
        m_connected = true;
        if (!m_status.host)
            m_discovery.Close();
        m_status.endpoint = m_punch->Peer();
        m_status.peer = m_status.endpoint.ip;
        m_status.state = State::Greeting;
        m_startedAt = now;
        m_stream = std::make_unique<UdpStream>(now, m_punch->StreamKey(m_status.host),
            m_punch->StreamKey(!m_status.host), [this](auto bytes) { m_io->Send(m_socket, bytes, m_status.endpoint); });
        if (m_status.host)
            RandomBytes(m_mine.nonce);
        else
            Send(MessageType::Hello, Encode(m_mine));
    }

    void Lobby::Poll()
    {
        if (m_socket == net::Invalid)
            return;
        const uint64_t now = Now();
        Discover(now);
        m_punch->Poll(now);
        // Bound work per window tick, even if a stranger floods the socket.
        for (size_t packets = 0; packets < 256; ++packets)
        {
            uint8_t buffer[2048];
            net::Endpoint from;
            const long got = m_io->Receive(m_socket, buffer, sizeof buffer, from);
            if (got < 0)
                break;
            const auto packet = std::span(buffer, size_t(got));
            bool stunAnswer = false;
            for (const auto& probe : m_probes)
                if (from == probe.server)
                {
                    stun::Endpoint mapped;
                    if (stun::ParseBindingResponse(packet, probe.id, mapped))
                    {
                        auto& seen = mapped.ip.find(':') == std::string::npos ? m_status.mapped.v4 : m_status.mapped.v6;
                        if (seen.ip != mapped.ip || seen.port != mapped.port)
                        {
                            seen = mapped;
                            SDL_Log("[online] public address (STUN): %s found",
                                mapped.ip.find(':') == std::string::npos ? "IPv4" : "IPv6");
                        }
                        stunAnswer = true;
                        break;
                    }
                }
            if (stunAnswer)
                continue;
            const bool other = m_connected && from != m_status.endpoint;
            const ByeReason refuse = m_status.host && now < m_refuseUntil ? ByeReason::Limited
                                     : m_status.host && other             ? ByeReason::Full
                                                                          : ByeReason(0);
            if (m_punch->Receive(from, packet, now, refuse))
            {
                if (!m_connected && m_punch->Connected())
                    Connected(now);
                if (refuse == ByeReason::Limited)
                    m_status.lastRefusal = { Error::TooManyCodes, Mismatch::None, "", now };
                continue;
            }
            if (m_stream && from == m_status.endpoint)
                m_stream->Receive(packet, now);
        }
        if (m_punch->Refused() != ByeReason(0))
        {
            End(m_punch->Refused() == ByeReason::Full ? Error::RoomFull : Error::TooManyCodes);
            return;
        }
        if (!m_stream)
        {
            if (m_punch->Expired(now))
            {
                SDL_Log("[online] no path to the host after 10 s");
                End(Error::CannotConnect);
            }
            return;
        }
        m_stream->Poll(now);
        ReadAll();
        if (!m_stream)
            return;
        if (m_failure)
        {
            Flush();
            if (m_stream && (m_stream->Drained() || m_stream->Closed()))
            {
                const auto [error, mismatch] = *m_failure;
                DropGuest(error, mismatch);
            }
            return;
        }
        if (m_stream->Closed())
        {
            Fail(Error::Timeout);
            return;
        }
        if (m_status.state == State::Greeting && now - m_startedAt >= ConnectTimeoutMs)
        {
            Fail(Error::Timeout);
            return;
        }
        if (m_status.state == State::InRoom && !m_startQueued && !m_guestStartAt && now >= m_nextPing)
        {
            Writer w;
            w.U64(now);
            Send(MessageType::Ping, w.Bytes());
            const bool testing = !m_status.testDone && now - m_testStart < TestMs;
            m_nextPing = now + (testing ? TestPingEveryMs : PingEveryMs);
            if (!testing && !m_status.testDone)
                FinishTest();
        }
        Flush();
        if (m_stream && ((m_startQueued && m_stream->Drained()) || (m_guestStartAt && now >= m_guestStartAt)))
        {
            m_status.state = State::Started;
            CloseConnection();
            m_io->Release(m_socket);
            m_socket = net::Invalid;
            m_discovery.Close();
        }
    }
}
