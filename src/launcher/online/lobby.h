#pragma once

// The online room (docs/online.md): the host opens it on a port, one guest joins with its address and the password; both
// see each other's profile and figures, the round trip and the input delay, and the host's settings and save go to the
// guest. Each player says Ready; the host starts, and both launchers then start the game with what the room agreed
// (online/session_launch.h). Polled from the window's loop: nothing here waits.
#include "launcher/online/lobby_protocol.h"
#include "launcher/online/net_socket.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace online
{
    struct SaveFile
    {
        std::string path; // relative, '/'-separated (SafeSavePath)
        std::vector<uint8_t> bytes;
    };

    struct HostOptions
    {
        uint16_t port = DefaultPort;
        std::string password;
        Hello hello; // this launcher and its game (host and nonce are filled in)
        Player player;
        Settings settings; // saveBytes and saveFiles are filled in from `save`
        std::vector<SaveFile> save;
    };

    struct JoinOptions
    {
        std::string address; // as pasted: "host:port"
        std::string password;
        Hello hello;
        Player player;
    };

    enum class State
    {
        Idle,
        Waiting,    // host: the room is open, nobody in it
        Connecting, // guest: reaching the host
        Greeting,   // hellos and proofs
        InRoom,
        Started,    // both start the game: Status::start holds what they agreed
        Ended,      // Status::error says why
    };

    enum class Error
    {
        None,
        PortInUse,     // host: the port cannot be opened
        BadAddress,    // guest: the address is not one
        CannotConnect, // guest: nothing answered there
        Timeout,       // the other side stopped answering
        Mismatch,      // not the same game, disc, version or system: Status::mismatch
        WrongPassword,
        RoomFull,
        Left,   // the other player left
        Broken, // malformed or unexpected messages
    };

    // A guest the host turned away while its room stays open (for the room's notice).
    struct Refusal
    {
        Error error = Error::None;
        Mismatch mismatch = Mismatch::None;
        std::string peer;
        uint64_t atMs = 0;
    };

    struct Status
    {
        State state = State::Idle;
        Error error = Error::None;
        Mismatch mismatch = Mismatch::None;
        bool host = false;
        std::string peer; // the other side's address, for the log
        Hello remoteHello;
        bool haveRemote = false;
        Player remote;
        bool localReady = false, remoteReady = false;
        double rttMs = -1; // -1 until the first pong
        uint8_t inputDelay = 2;
        // The guest's copy of the host's settings and save (the host's own are its options).
        bool haveSettings = false;
        Settings settings;
        uint32_t saveReceived = 0; // bytes
        std::vector<SaveFile> save;
        Refusal lastRefusal;
        Start start; // State::Started
        bool CanStart() const; // host: both ready, the guest has the save
    };

    class Lobby
    {
    public:
        using Clock = std::function<uint64_t()>; // milliseconds, increasing
        static constexpr uint64_t ConnectTimeoutMs = 10000;
        static constexpr uint64_t SilenceTimeoutMs = 15000; // no byte at all from the other side
        static constexpr uint64_t PingEveryMs = 1000;

        explicit Lobby(Clock clock = {});
        ~Lobby();
        Lobby(const Lobby&) = delete;
        Lobby& operator=(const Lobby&) = delete;

        bool Host(HostOptions options);
        bool Join(JoinOptions options);
        // The local player's figures or profile changed: sent again, and this side is no longer ready.
        void UpdatePlayer(const Player& player);
        void SetReady(bool ready);
        // Host only, when Status::CanStart(): tells the guest and moves both to State::Started.
        bool StartGame();
        void Leave();
        void Poll();
        const Status& Get() const { return m_status; }

    private:
        enum class Step
        {
            AwaitHello,
            AwaitProof,
            Room,
        };
        void Send(MessageType type, const std::vector<uint8_t>& payload);
        void SendBye(ByeReason reason);
        void Flush();
        bool ReadAll();
        void Handle(const Received& frame);
        void HandleHello(const Received& frame);
        void HandleProof(const Received& frame);
        void HandleRoom(const Received& frame);
        void SendRoomOpening();
        void DropGuest(Error error, Mismatch mismatch = Mismatch::None);
        void End(Error error, Mismatch mismatch = Mismatch::None);
        // The host turns the guest away and keeps its room open; the guest ends.
        void Fail(Error error, Mismatch mismatch = Mismatch::None);
        void CloseConnection();
        uint64_t Now() const;

        Clock m_clock;
        Status m_status;
        HostOptions m_hostOptions;
        JoinOptions m_joinOptions;
        Hello m_mine;
        Player m_player;
        net::Handle m_listener = net::Invalid;
        net::Handle m_connection = net::Invalid;
        bool m_connected = false;
        FrameReader m_reader;
        std::vector<uint8_t> m_out;
        size_t m_outStart = 0;
        Keys m_keys{};
        bool m_haveKeys = false;
        uint32_t m_sendSequence = 0, m_receiveSequence = 0;
        Step m_step = Step::AwaitHello;
        uint64_t m_startedAt = 0, m_lastHeard = 0, m_nextPing = 0;
        bool m_closeAfterFlush = false;
    };

    // A fresh random nonce or session key.
    void RandomBytes(std::span<uint8_t> out);
}
