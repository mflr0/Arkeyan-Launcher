#pragma once

// The online lobby's messages (docs/online.md): what two launchers say to each other before both start the game. One reliable UDP
// stream, the host listening; every frame is length-prefixed, and once both hellos are exchanged every frame carries a
// tag and encryption keyed from the room secret and fresh random nonces, so strangers are refused.
// Pure code: no socket here (online/lobby.h drives it), so the tests feed it bytes.
#include "launcher/online/chacha20.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace online
{
    inline constexpr uint16_t LobbyProtocolVersion = 3;
    inline constexpr uint16_t DefaultPort = 47640;
    inline constexpr size_t MaxFrameBytes = 4 * 1024 * 1024; // a whole frame: the biggest is a save chunk
    inline constexpr size_t SaveChunkBytes = 64 * 1024;
    inline constexpr size_t MaxPictureBytes = 512 * 1024;
    inline constexpr size_t MaxFigures = 16;
    inline constexpr size_t FigureDumpBytes = 1024;
    inline constexpr size_t TagBytes = 16;
    inline constexpr size_t NonceBytes = 32;
    inline constexpr size_t SessionKeyBytes = 16;

    enum class MessageType : uint8_t
    {
        Hello = 1,
        Proof = 2,
        Player = 3,
        Settings = 4,
        SaveChunk = 5,
        Ping = 6,
        Pong = 7,
        Ready = 8,
        Start = 9,
        Bye = 10,
    };

    using Nonce = std::array<uint8_t, NonceBytes>;
    using Tag = std::array<uint8_t, TagBytes>;

    struct Hello
    {
        uint16_t protocol = LobbyProtocolVersion;
        bool host = false;
        std::string launcherVersion; // "0.10.0"
        std::string gameId;          // "giants"
        std::string xexSha256;       // the disc's game, lowercase hex
        std::string gameVersion;     // the build the kit made ("0.8.16")
        std::string platform;        // "macos-arm64", "windows-x64", "linux-x64"
        bool crossPlatform = false;  // this player allows a session with another system
        Nonce nonce{};
    };

    struct Figure
    {
        uint16_t toyType = 0;
        uint16_t variant = 0;
        uint8_t seat = 0; // the catalogue seat it sits on (0: the first)
        std::string name;
        std::vector<uint8_t> dump; // FigureDumpBytes; for swappers two figures, one per half
    };

    struct Player
    {
        std::string pseudo;
        std::string xuid; // 16 hex digits
        uint8_t color = 0;
        std::vector<uint8_t> picture; // PNG, may be empty
        std::vector<Figure> figures;
        // This PC's pace in this game from its last log (0: unknown), and whether its GPU held it back: the room says
        // when the session will go at the slower one's pace, and that PC starts the session at a lower scale.
        uint8_t fps = 0;
        bool gpuBound = false;
    };

    // What the host's game uses and the guest's game must use too (docs/online.md, "Same game on both sides").
    struct Settings
    {
        uint8_t fpsLimit = 30; // 30 or 60
        std::string language;  // the game's text language code, "" for the game's default
        std::string voices;    // the voice language code, "" for the game's default
        bool nightmareFromStart = false;
        uint32_t saveBytes = 0; // the whole save that follows in chunks
        uint16_t saveFiles = 0;
    };

    struct SaveChunk
    {
        std::string file; // a relative path inside the game's save folder, '/'-separated, no ".."
        uint32_t fileBytes = 0;
        uint32_t offset = 0;
        std::vector<uint8_t> bytes;
    };

    struct Start
    {
        std::array<uint8_t, SessionKeyBytes> sessionKey{}; // the games' own packets are keyed from it
        uint8_t inputDelay = 2;                            // frames
        uint16_t port = DefaultPort;                       // the host's UDP port for the games
    };

    enum class ByeReason : uint8_t
    {
        Left = 1,       // the player left the room
        Full = 2,       // the room has its guest already
        Mismatch = 3,   // not the same game (the receiver compares the hellos itself for the detail)
        Code = 4,   // the tag did not match
        Protocol = 5,   // a malformed or unexpected message
        Timeout = 6,
        Limited = 7,
    };

    // Little-endian building blocks, bounds-checked: a reader that runs past its end fails and stays failed.
    class Writer
    {
    public:
        void U8(uint8_t value) { m_bytes.push_back(value); }
        void U16(uint16_t value);
        void U32(uint32_t value);
        void U64(uint64_t value);
        void Text(std::string_view text);           // u16 length
        void Blob(std::span<const uint8_t> bytes);  // u32 length
        void Raw(std::span<const uint8_t> bytes);
        std::vector<uint8_t>& Bytes() { return m_bytes; }

    private:
        std::vector<uint8_t> m_bytes;
    };

    class Reader
    {
    public:
        explicit Reader(std::span<const uint8_t> bytes) : m_bytes(bytes) {}
        uint8_t U8();
        uint16_t U16();
        uint32_t U32();
        uint64_t U64();
        std::string Text(size_t max);
        std::vector<uint8_t> Blob(size_t max);
        bool Raw(std::span<uint8_t> out);
        bool Ok() const { return m_ok; }
        bool AtEnd() const { return m_ok && m_at == m_bytes.size(); }

    private:
        bool Need(size_t count);
        std::span<const uint8_t> m_bytes;
        size_t m_at = 0;
        bool m_ok = true;
    };

    std::vector<uint8_t> Encode(const Hello& hello);
    std::vector<uint8_t> Encode(const Player& player);
    std::vector<uint8_t> Encode(const Settings& settings);
    std::vector<uint8_t> Encode(const SaveChunk& chunk);
    std::vector<uint8_t> Encode(const Start& start);
    bool Decode(std::span<const uint8_t> payload, Hello& hello);
    bool Decode(std::span<const uint8_t> payload, Player& player);
    bool Decode(std::span<const uint8_t> payload, Settings& settings);
    bool Decode(std::span<const uint8_t> payload, SaveChunk& chunk);
    bool Decode(std::span<const uint8_t> payload, Start& start);
    // A save file's name is safe to write under a scratch folder.
    bool SafeSavePath(std::string_view path);

    // Why two hellos cannot share a session; None when they can.
    enum class Mismatch : uint8_t
    {
        None,
        Protocol,
        Launcher,
        Game,
        Disc,
        GameVersion,
        Platform,
    };
    Mismatch Compare(const Hello& mine, const Hello& theirs);

    // HMAC-SHA256, and the keys of one direction of a session: from the five secret bytes and both nonces.
    std::array<uint8_t, 32> HmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> message);
    struct Keys
    {
        std::array<uint8_t, 32> hostToGuest{}, guestToHost{};
        CipherKey hostCipher{}, guestCipher{};
    };
    Keys DeriveKeys(uint64_t secret, const Nonce& hostNonce, const Nonce& guestNonce);
    // What each side proves it knows: the key of its own direction, tagged with its role.
    Tag ProofOf(const Keys& keys, bool host);

    // A frame on the wire: u32 length of what follows, u8 type, u32 sequence, payload, then the tag (zeros before keys).
    std::vector<uint8_t> Frame(
        MessageType type, uint32_t sequence, std::span<const uint8_t> payload, const Keys* keys, bool fromHost = false);

    struct Received
    {
        MessageType type{};
        uint32_t sequence = 0;
        std::vector<uint8_t> payload;
        Tag tag{};
    };
    // Bytes from the socket in, whole frames out. A length over MaxFrameBytes or under the header poisons it.
    class FrameReader
    {
    public:
        void Feed(std::span<const uint8_t> bytes);
        // The next whole frame, if any.
        std::optional<Received> Next();
        bool Broken() const { return m_broken; }

    private:
        std::vector<uint8_t> m_buffer;
        size_t m_start = 0;
        bool m_broken = false;
    };
    // Authenticate the ciphertext before decrypting it; failure leaves the payload untouched.
    bool OpenFrame(Received& frame, const Keys& keys, bool fromHost);
    // The tag a frame must carry under `key`.
    bool CheckTag(const Received& frame, const std::array<uint8_t, 32>& key);

    // Frames needed between input and output at `frameMs` per frame for a round trip of `rttMs`: ceil(rtt / 2 / frame) + 1,
    // from 1 to 8.
    // The connection test's verdict: 0 perfect, 1 playable, 2 slow, 3 very slow (docs/online.md, "The connection test").
    inline int ConnectionVerdict(double rttMs, double jitterMs)
    {
        const double felt = rttMs + jitterMs;
        return felt < 80 ? 0 : felt < 170 ? 1 : felt < 270 ? 2 : 3;
    }
    uint8_t InputDelayFor(double rttMs, double frameMs);
}
