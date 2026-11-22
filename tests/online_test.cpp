// ArkeyanLauncherTest's checks for the online room (launcher/online/): the messages and their limits, the keyed tags,
// addresses, then whole rooms over the loopback address: a guest that joins, gets the host's settings and save, and both
// start; a wrong room code, another disc, a full room, a guest that leaves, a silent one. Returns the number of failures.
#include "launcher/online/lobby.h"
#include <algorithm>
#include <limits>
#include "launcher/online/room_code.h"
#include "launcher/online/stun.h"
#include "launcher/online/session_launch.h"
#include "launcher/ui/collection_model.h"
#include "shared/collection/datadir.h"
#include <filesystem>
#include <fstream>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace online;

namespace
{
    int g_failures = 0;

#define ONLINE_EXPECT(condition)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition);                                  \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

    std::string Hex(std::span<const uint8_t> bytes)
    {
        std::string out;
        char two[3];
        for (const uint8_t b : bytes)
        {
            snprintf(two, sizeof(two), "%02x", b);
            out += two;
        }
        return out;
    }

    std::span<const uint8_t> Bytes(std::string_view text)
    {
        return { reinterpret_cast<const uint8_t*>(text.data()), text.size() };
    }

    Hello SampleHello()
    {
        Hello hello;
        hello.launcherVersion = "0.10.0";
        hello.gameId = "giants";
        hello.xexSha256 = std::string(64, 'a');
        hello.gameVersion = "0.8.16";
        hello.platform = "macos-arm64";
        return hello;
    }

    Player SamplePlayer(const char* pseudo, uint8_t figureByte)
    {
        Player player;
        player.pseudo = pseudo;
        player.xuid = "E0000000000000AB";
        player.color = 3;
        player.picture = { 0x89, 'P', 'N', 'G' };
        Figure figure;
        figure.toyType = 14;
        figure.name = "Spyro";
        figure.dump.assign(FigureDumpBytes, figureByte);
        player.figures.push_back(figure);
        return player;
    }

    void CipherChecks()
    {
        CipherKey key{};
        for (size_t i = 0; i < key.size(); ++i)
            key[i] = uint8_t(i);
        CipherNonce nonce{ 0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0 };
        // RFC 8439 sections 2.3.2 and 2.4.2.
        ONLINE_EXPECT(Hex(ChaCha20Block(key, nonce, 1)) ==
                      "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
                      "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e");
        nonce[3] = 0;
        const std::string plain = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
                                  "future, sunscreen would be it.";
        std::vector<uint8_t> bytes(plain.begin(), plain.end());
        ONLINE_EXPECT(ChaCha20Xor(key, nonce, 1, bytes));
        ONLINE_EXPECT(Hex(bytes) == "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
                                    "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
                                    "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
                                    "5af90bbf74a35be6b40b8eedf2785e42874d");
        ONLINE_EXPECT(ChaCha20Xor(key, nonce, 1, bytes) && std::string(bytes.begin(), bytes.end()) == plain);
        ONLINE_EXPECT(!ChaCha20Xor(key, nonce, std::numeric_limits<uint32_t>::max(), bytes));
        ONLINE_EXPECT(std::string(bytes.begin(), bytes.end()) == plain);
    }

    void MessageChecks()
    {
        // RFC 4231, test case 2.
        ONLINE_EXPECT(Hex(HmacSha256(Bytes("Jefe"), Bytes("what do ya want for nothing?"))) ==
                      "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
        // A key longer than a block is hashed first (RFC 4231, test case 6).
        const std::vector<uint8_t> longKey(131, 0xAA);
        ONLINE_EXPECT(Hex(HmacSha256(longKey, Bytes("Test Using Larger Than Block-Size Key - Hash Key First"))) ==
                      "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

        Hello hello = SampleHello();
        hello.host = true;
        hello.crossPlatform = true;
        hello.nonce[5] = 9;
        Hello back;
        ONLINE_EXPECT(Decode(Encode(hello), back) && back.host && back.crossPlatform && back.gameId == "giants" &&
                      back.nonce[5] == 9 && back.platform == "macos-arm64");
        auto truncated = Encode(hello);
        truncated.pop_back();
        ONLINE_EXPECT(!Decode(truncated, back));
        auto longer = Encode(hello);
        longer.push_back(0);
        ONLINE_EXPECT(!Decode(longer, back));
        // A newer protocol is read far enough to say so.
        std::vector<uint8_t> future = { uint8_t(LobbyProtocolVersion + 1), 0, 0xFF };
        ONLINE_EXPECT(Decode(future, back) && back.protocol == LobbyProtocolVersion + 1);

        Player player = SamplePlayer("Kaos", 7), playerBack;
        ONLINE_EXPECT(Decode(Encode(player), playerBack) && playerBack.pseudo == "Kaos" &&
                      playerBack.figures.size() == 1 && playerBack.figures[0].dump[1023] == 7);
        player.xuid = "nothex0000000000";
        ONLINE_EXPECT(!Decode(Encode(player), playerBack));
        player = SamplePlayer("Kaos", 7);
        player.figures[0].dump.resize(1000);
        ONLINE_EXPECT(!Decode(Encode(player), playerBack));
        player = SamplePlayer("Kaos", 7);
        player.figures.assign(MaxFigures + 1, player.figures[0]);
        ONLINE_EXPECT(!Decode(Encode(player), playerBack));

        Settings settings{ 60, "fr", "en", true, 1234, 2 }, settingsBack;
        ONLINE_EXPECT(Decode(Encode(settings), settingsBack) && settingsBack.fpsLimit == 60 &&
                      settingsBack.language == "fr" && settingsBack.nightmareFromStart && settingsBack.saveFiles == 2);
        settings.fpsLimit = 45;
        ONLINE_EXPECT(!Decode(Encode(settings), settingsBack));

        ONLINE_EXPECT(SafeSavePath("slot0/save.bin") && SafeSavePath("Skylanders Giants.sav"));
        ONLINE_EXPECT(!SafeSavePath("../x") && !SafeSavePath("/etc/x") && !SafeSavePath("a//b") &&
                      !SafeSavePath("a/./b") && !SafeSavePath("a\\b") && !SafeSavePath("") && !SafeSavePath("a/"));
        SaveChunk chunk{ "slot0/save.bin", 10, 8, { 1, 2 } }, chunkBack;
        ONLINE_EXPECT(Decode(Encode(chunk), chunkBack) && chunkBack.offset == 8);
        chunk.offset = 9;
        ONLINE_EXPECT(!Decode(Encode(chunk), chunkBack));
        chunk = { "../x", 2, 0, { 1, 2 } };
        ONLINE_EXPECT(!Decode(Encode(chunk), chunkBack));

        Start start, startBack;
        start.sessionKey[0] = 42;
        start.inputDelay = 3;
        ONLINE_EXPECT(Decode(Encode(start), startBack) && startBack.sessionKey[0] == 42 && startBack.inputDelay == 3);
        start.inputDelay = 9;
        ONLINE_EXPECT(!Decode(Encode(start), startBack));

        Hello a = SampleHello(), b = SampleHello();
        ONLINE_EXPECT(Compare(a, b) == Mismatch::None);
        b.xexSha256[0] = 'b';
        ONLINE_EXPECT(Compare(a, b) == Mismatch::Disc);
        b = SampleHello();
        b.gameId = "ssa";
        ONLINE_EXPECT(Compare(a, b) == Mismatch::Game);
        b = SampleHello();
        b.gameVersion = "0.8.15";
        ONLINE_EXPECT(Compare(a, b) == Mismatch::GameVersion);
        b = SampleHello();
        b.platform = "windows-x64";
        ONLINE_EXPECT(Compare(a, b) == Mismatch::Platform);
        a.crossPlatform = b.crossPlatform = true;
        ONLINE_EXPECT(Compare(a, b) == Mismatch::None);

        Nonce n1{}, n2{};
        n1[0] = 1;
        n2[0] = 2;
        const Keys k1 = DeriveKeys(0x123456789a, n1, n2), k2 = DeriveKeys(0x123456789a, n1, n2),
                   k3 = DeriveKeys(0x123456789b, n1, n2), k4 = DeriveKeys(0x123456789a, n2, n1);
        ONLINE_EXPECT(k1.hostToGuest == k2.hostToGuest && k1.hostToGuest != k3.hostToGuest &&
                      k1.hostToGuest != k4.hostToGuest && k1.hostToGuest != k1.guestToHost);
        ONLINE_EXPECT(ProofOf(k1, true) != ProofOf(k1, false));

        // Frames: whole ones out of bytes fed one at a time; the tag binds type, sequence and payload.
        const std::vector<uint8_t> payload = { 1, 2, 3 };
        const auto frame = Frame(MessageType::Ready, 7, payload, &k1, true);
        FrameReader reader;
        int count = 0;
        for (const uint8_t byte : frame)
        {
            reader.Feed(std::span(&byte, 1));
            while (auto got = reader.Next())
            {
                ++count;
                ONLINE_EXPECT(got->type == MessageType::Ready && got->sequence == 7 && got->payload != payload);
                ONLINE_EXPECT(CheckTag(*got, k1.hostToGuest) && !CheckTag(*got, k1.guestToHost));
                got->payload[0] ^= 1;
                ONLINE_EXPECT(!CheckTag(*got, k1.hostToGuest));
                got->payload[0] ^= 1;
                got->sequence++;
                ONLINE_EXPECT(!CheckTag(*got, k1.hostToGuest));
                got->sequence--;
                ONLINE_EXPECT(OpenFrame(*got, k1, true) && got->payload == payload);
            }
        }
        ONLINE_EXPECT(count == 1 && !reader.Broken());
        FrameReader oversized;
        const uint8_t huge[] = { 0xFF, 0xFF, 0xFF, 0x7F, 0 };
        oversized.Feed(huge);
        ONLINE_EXPECT(!oversized.Next() && oversized.Broken());
        FrameReader tiny;
        const uint8_t small[] = { 2, 0, 0, 0, 0, 0 };
        tiny.Feed(small);
        ONLINE_EXPECT(!tiny.Next() && tiny.Broken());

        // Random bytes never crash a decoder (the sanitizers build watches the reads).
        std::srand(5);
        for (int i = 0; i < 20000; ++i)
        {
            std::vector<uint8_t> noise(size_t(std::rand() % 64));
            for (auto& byte : noise)
                byte = uint8_t(std::rand());
            Hello h;
            Player p;
            Settings s;
            SaveChunk c;
            Start st;
            Decode(noise, h);
            Decode(noise, p);
            Decode(noise, s);
            Decode(noise, c);
            Decode(noise, st);
            FrameReader r;
            r.Feed(noise);
            while (r.Next())
            {}
        }

        ONLINE_EXPECT(InputDelayFor(0, 33.3) == 1 && InputDelayFor(60, 33.3) == 2 && InputDelayFor(100, 33.3) == 3 &&
                      InputDelayFor(100, 16.7) == 4 && InputDelayFor(5000, 16.7) == 8);

        std::string host;
        uint16_t port = 0;
        ONLINE_EXPECT(
            net::ParseAddress(" 203.0.113.7:5000 ", 47640, host, port) && host == "203.0.113.7" && port == 5000);
        ONLINE_EXPECT(net::ParseAddress("203.0.113.7", 47640, host, port) && port == 47640);
        ONLINE_EXPECT(
            net::ParseAddress("[2001:db8::1]:6000", 47640, host, port) && host == "2001:db8::1" && port == 6000);
        ONLINE_EXPECT(net::ParseAddress("2001:db8::1", 47640, host, port) && host == "2001:db8::1" && port == 47640);
        ONLINE_EXPECT(net::ParseAddress("my-pc.local:7", 47640, host, port) && host == "my-pc.local" && port == 7);
        ONLINE_EXPECT(!net::ParseAddress("", 1, host, port) && !net::ParseAddress("a:0", 1, host, port) &&
                      !net::ParseAddress("a:70000", 1, host, port) && !net::ParseAddress("a:", 1, host, port) &&
                      !net::ParseAddress("[::1", 1, host, port) && !net::ParseAddress("a b:1", 1, host, port));

        // Room codes: the three lengths, read back whatever the case, dashes and look-alike letters; a typo refused.
        constexpr uint64_t secret = 0x9876543210;
        uint64_t decoded = 0;
        const std::string shortCode = EncodeRoomCode("203.0.113.4", DefaultPort, secret);
        ONLINE_EXPECT(shortCode.size() == 24 && shortCode[4] == '-');
        ONLINE_EXPECT(DecodeRoomCode(shortCode, host, port, decoded) && host == "203.0.113.4" && port == DefaultPort &&
                      decoded == secret);
        const std::string withPort = EncodeRoomCode("203.0.113.4", 5000, secret);
        ONLINE_EXPECT(withPort.size() == 29 && withPort != shortCode);
        ONLINE_EXPECT(DecodeRoomCode(withPort, host, port, decoded) && host == "203.0.113.4" && port == 5000 &&
                      decoded == secret);
        const std::string v6 = EncodeRoomCode("2001:db8::7", 47641, secret);
        ONLINE_EXPECT(v6.size() == 54);
        ONLINE_EXPECT(
            DecodeRoomCode(v6, host, port, decoded) && host == "2001:db8::7" && port == 47641 && decoded == secret);
        ONLINE_EXPECT(EncodeRoomCode("my-pc.local", 1, secret).empty() && EncodeRoomCode("", 1, secret).empty());
        {
            std::string loose;
            for (const char c : shortCode)
                if (c != '-')
                    loose += char(c == '0' ? 'o' : c == '1' ? 'l' : (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
            ONLINE_EXPECT(
                DecodeRoomCode(" " + loose.substr(0, 4) + " " + loose.substr(4) + "\n", host, port, decoded) &&
                host == "203.0.113.4");
            int refused = 0;
            for (size_t i = 0; i < shortCode.size(); ++i)
            {
                if (shortCode[i] == '-')
                    continue;
                std::string typo = shortCode;
                typo[i] = typo[i] == 'Z' ? 'Y' : char(typo[i] == '9' ? 'A' : typo[i] + 1);
                std::string h;
                uint16_t p = 0;
                refused += DecodeRoomCode(typo, h, p, decoded) ? 0 : 1;
            }
            ONLINE_EXPECT(refused == 20);
        }
        ONLINE_EXPECT(EncodeRoomCode("203.0.113.4", DefaultPort, secret + 1) != shortCode);
        ONLINE_EXPECT(EncodeRoomCode("203.0.113.4", DefaultPort, secret | (0xffull << 40)) == shortCode);
        ONLINE_EXPECT(!DecodeRoomCode("K7QD-M2X", host, port, decoded) &&
                      !DecodeRoomCode("K7QD-M2XU", host, port, decoded) && !DecodeRoomCode("", host, port, decoded));
        ONLINE_EXPECT(net::ParseAddress(withPort, DefaultPort, host, port) && host == "203.0.113.4" && port == 5000);
        ONLINE_EXPECT(net::ParseAddress(v6, DefaultPort, host, port) && host == "2001:db8::7" && port == 47641);

        // STUN: the request's header, and the XOR-mapped address of an answer (IPv4, IPv6), the plain one as a fallback.
        const stun::Id id{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        const auto request = stun::BindingRequest(id);
        ONLINE_EXPECT(request.size() == 20 && request[0] == 0 && request[1] == 1 && request[2] == 0 &&
                      request[3] == 0 && request[4] == 0x21 && request[5] == 0x12 && request[6] == 0xA4 &&
                      request[7] == 0x42 && request[8] == 1 && request[19] == 12);
        const auto answer = [&](uint16_t type, std::vector<uint8_t> value) {
            std::vector<uint8_t> packet = { 0x01, 0x01, 0, uint8_t(4 + value.size()), 0x21, 0x12, 0xA4, 0x42 };
            packet.insert(packet.end(), id.begin(), id.end());
            packet.push_back(uint8_t(type >> 8));
            packet.push_back(uint8_t(type));
            packet.push_back(0);
            packet.push_back(uint8_t(value.size()));
            packet.insert(packet.end(), value.begin(), value.end());
            return packet;
        };
        stun::Endpoint mapped;
        // 203.0.113.4:5000 XORed with the magic cookie.
        const auto v4 = answer(0x0020, { 0, 1, uint8_t((5000 ^ 0x2112) >> 8), uint8_t((5000 ^ 0x2112) & 0xFF),
                                           0xCB ^ 0x21, 0x00 ^ 0x12, 0x71 ^ 0xA4, 0x04 ^ 0x42 });
        ONLINE_EXPECT(stun::ParseBindingResponse(v4, id, mapped) && mapped.ip == "203.0.113.4" && mapped.port == 5000);
        std::vector<uint8_t> v6Value = { 0, 2, 0x00 ^ 0x21, 0x50 ^ 0x12 };
        const uint8_t address6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7 };
        const uint8_t key[16] = { 0x21, 0x12, 0xA4, 0x42, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        for (int i = 0; i < 16; ++i)
            v6Value.push_back(uint8_t(address6[i] ^ key[i]));
        ONLINE_EXPECT(stun::ParseBindingResponse(answer(0x0020, v6Value), id, mapped) && mapped.ip == "2001:db8::7" &&
                      mapped.port == 0x50);
        ONLINE_EXPECT(stun::ParseBindingResponse(answer(0x0001, { 0, 1, 0x13, 0x88, 198, 51, 100, 7 }), id, mapped) &&
                      mapped.ip == "198.51.100.7" && mapped.port == 5000);
        stun::Id other = id;
        other[0] = 9;
        ONLINE_EXPECT(!stun::ParseBindingResponse(v4, other, mapped));
        auto cut = v4;
        cut.resize(cut.size() - 2);
        ONLINE_EXPECT(!stun::ParseBindingResponse(cut, id, mapped));
        auto error = v4;
        error[1] = 0x11;
        ONLINE_EXPECT(!stun::ParseBindingResponse(error, id, mapped));
    }

    // Two rooms' worth of polling on one thread, on a clock the test moves.
    struct Room
    {
        uint64_t now = 1000;
        bool ticking = false; // each reading of the clock a millisecond later, as a real clock moves during a poll
        Lobby host{ [this] { return ticking ? now++ : now; } };
        Lobby guest{ [this] { return ticking ? now++ : now; } };
        uint16_t port = 0;

        bool Open(std::vector<SaveFile> save = {})
        {
            HostOptions options;
            options.hello = SampleHello();
            options.player = SamplePlayer("Host", 1);
            options.settings.fpsLimit = 30;
            options.settings.language = "fr";
            options.save = std::move(save);
            // A port of this test's own, away from the default the player's launcher may hold.
            for (uint16_t candidate = 47700; candidate < 47760; ++candidate)
            {
                options.port = candidate;
                if (host.Host(options))
                {
                    port = candidate;
                    return true;
                }
            }
            return false;
        }

        void Join(Lobby& who, bool wrong = false, Hello hello = SampleHello())
        {
            JoinOptions options;
            options.address = EncodeRoomCode("127.0.0.1", port, host.Secret() ^ uint64_t(wrong));
            options.hello = hello;
            options.player = SamplePlayer("Guest", 2);
            options.player.xuid = "E0000000000000CD";
            who.Join(options);
        }

        // Polls both until `done` or 3000 rounds (each a few ms of the test clock).
        template <typename Done>
        bool Until(Done done, Lobby* other = nullptr)
        {
            for (int i = 0; i < 3000; ++i)
            {
                host.Poll();
                guest.Poll();
                if (other)
                    other->Poll();
                if (done())
                    return true;
                now += 5;
                SDL_Delay(1);
            }
            return false;
        }
    };

    struct WireGuest
    {
        net::Handle socket = net::Invalid;
        FrameReader reader;
        Keys keys;
        std::vector<uint8_t> wire;
        ~WireGuest() { net::Close(socket); }

        bool Send(const std::vector<uint8_t>& frame)
        {
            size_t at = 0;
            for (int i = 0; i < 3000 && at < frame.size(); ++i)
            {
                const long sent = net::Send(socket, frame.data() + at, frame.size() - at);
                if (sent < 0)
                    return false;
                at += size_t(sent);
                if (!sent)
                    SDL_Delay(1);
            }
            return at == frame.size();
        }

        void Read()
        {
            uint8_t bytes[16384];
            for (;;)
            {
                const long got = net::Receive(socket, bytes, sizeof bytes);
                if (got <= 0)
                    break;
                wire.insert(wire.end(), bytes, bytes + got);
                reader.Feed(std::span(bytes, size_t(got)));
            }
        }

        bool Enter(Room& room)
        {
            std::string error;
            socket = net::Connect("127.0.0.1", room.port, error);
            if (socket == net::Invalid ||
                !room.Until([&] { return net::Connecting(socket) == net::Progress::Connected; }))
                return false;
            Hello hello = SampleHello();
            RandomBytes(hello.nonce);
            if (!Send(Frame(MessageType::Hello, 0, Encode(hello), nullptr)))
                return false;
            std::optional<Received> reply;
            if (!room.Until([&] {
                    Read();
                    reply = reader.Next();
                    return bool(reply);
                }))
                return false;
            Hello hostHello;
            if (reply->type != MessageType::Hello || !Decode(reply->payload, hostHello))
                return false;
            keys = DeriveKeys(room.host.Secret(), hostHello.nonce, hello.nonce);
            const auto proof = ProofOf(keys, false);
            if (!Send(Frame(MessageType::Proof, 1, proof, &keys, false)))
                return false;
            return room.Until([&] {
                Read();
                return room.host.Get().state == State::InRoom;
            });
        }
    };

    void SecurityChecks()
    {
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            const auto secret = room.host.Secret();
            ONLINE_EXPECT((secret >> 40) == 0);
            ONLINE_EXPECT(room.Open() && room.host.Secret() != secret);
            JoinOptions plain;
            plain.address = "127.0.0.1:" + std::to_string(room.port);
            ONLINE_EXPECT(!room.guest.Join(plain) && room.guest.Get().error == Error::CodeRequired);
            room.host.Poll();
            ONLINE_EXPECT(room.host.Get().state == State::Waiting);
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                room.Join(room.guest, true);
                ONLINE_EXPECT(room.Until(
                    [&] { return room.guest.Get().state == State::Ended && room.host.Get().state == State::Waiting; }));
                ONLINE_EXPECT(room.guest.Get().error == Error::WrongCode);
                ONLINE_EXPECT(
                    room.host.Get().lastRefusal.error == (attempt == 4 ? Error::TooManyCodes : Error::WrongCode));
            }
            const uint64_t refusedAt = room.host.Get().lastRefusal.atMs;
            room.now = refusedAt + 29900;
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Ended; }));
            ONLINE_EXPECT(room.guest.Get().error == Error::TooManyCodes);
            room.now = refusedAt + 30000;
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                room.Join(room.guest, true);
                ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Ended; }));
                ONLINE_EXPECT(room.host.Get().lastRefusal.error == Error::WrongCode);
                room.now += 60001;
            }
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
        }
        const std::string saveText = "stand-in save bytes that must stay private";
        for (int attack = 0; attack < 5; ++attack)
        {
            Room room;
            ONLINE_EXPECT(room.Open({ { "slot0/save.bin", std::vector<uint8_t>(saveText.begin(), saveText.end()) } }));
            WireGuest guest;
            if (!guest.Enter(room))
            {
                ONLINE_EXPECT(false);
                continue;
            }
            bool receivedSave = false, receivedPlayer = false, receivedProof = false;
            ONLINE_EXPECT(room.Until([&] {
                guest.Read();
                while (auto frame = guest.reader.Next())
                {
                    ONLINE_EXPECT(OpenFrame(*frame, guest.keys, true));
                    if (frame->type == MessageType::SaveChunk)
                    {
                        SaveChunk chunk;
                        ONLINE_EXPECT(Decode(frame->payload, chunk) &&
                                      std::string(chunk.bytes.begin(), chunk.bytes.end()) == saveText);
                        receivedSave = true;
                    }
                    if (frame->type == MessageType::Player)
                    {
                        Player player;
                        ONLINE_EXPECT(Decode(frame->payload, player) && player.pseudo == "Host");
                        receivedPlayer = true;
                    }
                    if (frame->type == MessageType::Proof)
                    {
                        const auto expected = ProofOf(guest.keys, true);
                        ONLINE_EXPECT(
                            std::equal(frame->payload.begin(), frame->payload.end(), expected.begin(), expected.end()));
                        receivedProof = true;
                    }
                }
                return receivedSave && receivedPlayer && receivedProof;
            }));
            const auto contains = [&](std::string_view plain) {
                return std::search(guest.wire.begin(), guest.wire.end(), plain.begin(), plain.end()) !=
                       guest.wire.end();
            };
            ONLINE_EXPECT(!contains("Host") && !contains(saveText));
            std::vector<uint8_t> payload{ 1 };
            MessageType type = MessageType::Ready;
            if (attack == 2)
            {
                type = MessageType::Player;
                Player large = SamplePlayer("Guest", 2);
                large.pseudo.assign(65, 'x');
                payload = Encode(large);
            }
            else if (attack == 3)
            {
                type = MessageType::SaveChunk;
                payload = Encode(SaveChunk{ "slot0/save.bin", 1, 1, { 2 } });
            }
            else if (attack == 4)
                type = MessageType(255);
            auto frame = Frame(type, 2, payload, &guest.keys, false);
            if (attack == 0)
            {
                frame[9] ^= 1;
                FrameReader check;
                check.Feed(frame);
                auto changed = check.Next();
                ONLINE_EXPECT(changed && !OpenFrame(*changed, guest.keys, false));
            }
            if (attack == 1)
            {
                ONLINE_EXPECT(guest.Send(frame));
                ONLINE_EXPECT(room.Until([&] { return room.host.Get().remoteReady; }));
            }
            ONLINE_EXPECT(guest.Send(frame));
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().state == State::Waiting; }));
            ONLINE_EXPECT(room.host.Get().lastRefusal.error == Error::Broken);
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
        }
    }

    void RoomChecks()
    {
        {
            // The clock moves between the start of a poll and the bytes it reads: not a silence.
            Room room;
            room.ticking = true;
            ONLINE_EXPECT(room.Open());
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until(
                [&] { return room.host.Get().state == State::InRoom && room.guest.Get().state == State::InRoom; }));
            ONLINE_EXPECT(room.host.Get().lastRefusal.error == Error::None);
        }
        {
            std::vector<SaveFile> save = { { "slot0/save.bin", std::vector<uint8_t>(150000) },
                { "options.cfg", { 1, 2 } } };
            for (size_t i = 0; i < save[0].bytes.size(); ++i)
                save[0].bytes[i] = uint8_t(i * 7);
            Room room;
            ONLINE_EXPECT(room.Open(save) && room.host.Get().state == State::Waiting);
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] {
                const Status& g = room.guest.Get();
                return g.state == State::InRoom && g.haveRemote && g.saveReceived == 150002 &&
                       room.host.Get().haveRemote && room.host.Get().rttMs >= 0;
            }));
            const Status& g = room.guest.Get();
            ONLINE_EXPECT(g.remote.pseudo == "Host" && room.host.Get().remote.pseudo == "Guest" &&
                          room.host.Get().remote.xuid == "E0000000000000CD");
            ONLINE_EXPECT(g.settings.language == "fr" && g.save.size() == 2 && g.save[0].bytes == save[0].bytes &&
                          g.save[1].path == "options.cfg");
            ONLINE_EXPECT(!room.host.StartGame()); // nobody is ready
            room.host.SetReady(true);
            room.guest.SetReady(true);
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().CanStart() && room.guest.Get().remoteReady; }));
            // A figure changed: that player is no longer ready, and the other side sees the new figure.
            Player changed = SamplePlayer("Guest", 9);
            changed.xuid = "E0000000000000CD";
            room.guest.UpdatePlayer(changed);
            ONLINE_EXPECT(room.Until(
                [&] { return !room.host.Get().remoteReady && room.host.Get().remote.figures[0].dump[0] == 9; }));
            ONLINE_EXPECT(!room.host.StartGame());
            room.guest.SetReady(true);
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().CanStart(); }));
            ONLINE_EXPECT(room.host.StartGame());
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Started; }));
            ONLINE_EXPECT(room.host.Get().state == State::Started &&
                          room.host.Get().start.sessionKey == room.guest.Get().start.sessionKey &&
                          room.guest.Get().start.port == room.port &&
                          room.host.Get().start.inputDelay == room.guest.Get().start.inputDelay);
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            room.Join(room.guest, true);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Ended; }));
            ONLINE_EXPECT(room.guest.Get().error == Error::WrongCode);
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().lastRefusal.error == Error::WrongCode; }));
            ONLINE_EXPECT(room.host.Get().state == State::Waiting);
            // The room stays open: the right code gets in after a wrong one.
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            Hello other = SampleHello();
            other.xexSha256 = std::string(64, 'b');
            room.Join(room.guest, false, other);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Ended; }));
            ONLINE_EXPECT(room.guest.Get().error == Error::Mismatch && room.guest.Get().mismatch == Mismatch::Disc);
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().lastRefusal.mismatch == Mismatch::Disc; }));
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
            Lobby third([&room] { return room.now; });
            room.Join(third);
            ONLINE_EXPECT(room.Until([&] { return third.Get().state == State::Ended; }, &third));
            ONLINE_EXPECT(third.Get().error == Error::RoomFull && room.guest.Get().state == State::InRoom);
            room.guest.Leave();
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().state == State::Waiting; }));
            ONLINE_EXPECT(room.host.Get().lastRefusal.error == Error::Left);
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open());
            room.Join(room.guest);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
            // The host stops answering (its window froze): the guest gives up after the silence timeout.
            for (int i = 0; i < 40 && room.guest.Get().state == State::InRoom; ++i)
            {
                room.now += 500;
                room.guest.Poll();
            }
            ONLINE_EXPECT(room.guest.Get().state == State::Ended && room.guest.Get().error == Error::Timeout);
        }
        {
            Lobby guest;
            JoinOptions options;
            options.address = "not an address";
            options.hello = SampleHello();
            options.player = SamplePlayer("Guest", 2);
            ONLINE_EXPECT(!guest.Join(options) && guest.Get().error == Error::BadAddress);
        }
    }
}

namespace
{
    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    std::string Variable(const GameLaunch& launch, const std::string& name)
    {
        std::string value = "<unset>";
        int count = 0;
        for (const auto& [key, v] : launch.variables)
            if (key == name)
            {
                value = v;
                ++count;
            }
        return count > 1 ? "<twice>" : value;
    }

    void SessionChecks(const std::filesystem::path& scratch)
    {
        namespace fs = std::filesystem;
        const collection::Game* game = collection::Catalogue::Get().FindGame("giants");
        ONLINE_EXPECT(game != nullptr);
        if (!game)
            return;
        size_t p1 = 99, p2 = 99, shared = 99;
        for (size_t i = 0; i < game->seats.size(); ++i)
        {
            if (game->seats[i].player == 1 && p1 == 99)
                p1 = i;
            else if (game->seats[i].player == 2 && p2 == 99)
                p2 = i;
            else if (game->seats[i].player == 0 && shared == 99)
                shared = i;
        }
        ONLINE_EXPECT(p1 != 99 && p2 != 99);

        Player host = SamplePlayer("Host", 1), guest = SamplePlayer("Guest", 2);
        guest.xuid = "E0000000000000CD";
        guest.figures[0].seat = uint8_t(p1); // the guest's own player 1 seat
        Figure onTheirP2 = guest.figures[0];
        onTheirP2.seat = uint8_t(p2); // a guest's own player-2 figure has no place: Player 2 is the guest
        guest.figures.push_back(onTheirP2);
        host.figures[0].seat = uint8_t(p1);
        if (shared != 99)
        {
            Figure item = host.figures[0];
            item.seat = uint8_t(shared);
            host.figures.push_back(item);
            Figure guestItem = guest.figures[0];
            guestItem.seat = uint8_t(shared);
            guest.figures.push_back(guestItem);
        }
        const auto seats = SessionSeats(*game, host, guest);
        ONLINE_EXPECT(seats[p1] == "p1-0.sky" && seats[p2] == "p2-0.sky");
        if (shared != 99)
            ONLINE_EXPECT(seats[shared] == "p1-1.sky");
        ONLINE_EXPECT(std::count_if(seats.begin(), seats.end(), [](const std::string& s) { return !s.empty(); }) ==
                      (shared != 99 ? 3 : 2));

        const fs::path root = scratch / "online-session", realSaves = scratch / "online-real-saves",
                       collectionDir = scratch / "online-collection";
        std::error_code ec;
        fs::remove_all(realSaves, ec);
        fs::remove_all(collectionDir, ec);
        fs::create_directories(realSaves / "slot0");
        fs::create_directories(collectionDir);
        std::ofstream(realSaves / "slot0" / "save.bin", std::ios::binary) << "old save";
        const std::string realPortal = "0 /figures/Spyro.sky\n";
        std::ofstream(realSaves / "portal.txt", std::ios::binary) << realPortal;
        std::ofstream(collectionDir / "Spyro.sky", std::ios::binary) << std::string(FigureDumpBytes, char(1));

        SessionPlan plan;
        plan.host = true;
        plan.start.port = 47641;
        plan.start.inputDelay = 3;
        plan.start.sessionKey[0] = 0xAB;
        plan.hostPlayer = host;
        plan.guestPlayer = guest;
        plan.settings.fpsLimit = 60;
        plan.settings.language = "fr";
        plan.save = { { "slot0/save.bin", { 'n', 'e', 'w' } }, { "portal.txt", { '0', '\n' } } };
        plan.localFigureFiles = { datadir::Text(collectionDir / "Spyro.sky") };
        GameLaunch launch;
        launch.variables = { { "SKY_SAVE_DIR", "/elsewhere" }, { "SKY_PORTAL_PLACE", "x" },
            { "SKY_VOICE_LANGUAGE", "de" } };
        std::string error;
        ONLINE_EXPECT(PrepareSession(*game, plan, root, launch, error));
        ONLINE_EXPECT(Variable(launch, "SKY_SAVE_DIR") == datadir::Text(root / "saves"));
        ONLINE_EXPECT(
            Variable(launch, "SKY_PORTAL_PLACE") == "<unset>" && Variable(launch, "SKY_VOICE_LANGUAGE") == "<unset>");
        ONLINE_EXPECT(Variable(launch, "SKY_PLAYER_NAME") == "Host" &&
                      Variable(launch, "SKY_PLAYER2_NAME") == "Guest" &&
                      Variable(launch, "SKY_PLAYER2_XUID") == "E0000000000000CD");
        ONLINE_EXPECT(
            Variable(launch, "SKY_NETPLAY_HOST") == "47641" && Variable(launch, "SKY_NETPLAY_JOIN") == "<unset>");
        ONLINE_EXPECT(Variable(launch, "SKY_NETPLAY_DELAY") == "3" && Variable(launch, "SKY_NETPLAY_FPS") == "60" &&
                      Variable(launch, "SKY_LANGUAGE") == "fr" &&
                      Variable(launch, "SKY_NETPLAY_SCHEDULE") == "cooperative");
        ONLINE_EXPECT(Variable(launch, "SKY_NETPLAY_PASSWORD").size() == 32 &&
                      Variable(launch, "SKY_NETPLAY_PASSWORD").starts_with("ab"));
        ONLINE_EXPECT(ReadText(root / "saves" / "slot0" / "save.bin") == "new");
        ONLINE_EXPECT(ReadText(root / "pictures" / "p2.png").size() == 4);
        // The seats as the game reads them: "<slot> <path>" lines (the stand-in dumps are not real figures, so the
        // launcher's own reader, which reads each figure, would not seat them).
        const std::string portal = ReadText(root / "saves" / "portal.txt");
        const auto line = [&](size_t seat, const char* name) {
            return std::to_string(launcher_ui::SeatFirstSlot(*game, seat)) + " " +
                   datadir::Text(root / "figures" / name) + "\n";
        };
        ONLINE_EXPECT(portal.find(line(p1, "p1-0.sky")) != std::string::npos &&
                      portal.find(line(p2, "p2-0.sky")) != std::string::npos);

        // The game played: the save and the host's figure changed in the session folder.
        std::ofstream(root / "saves" / "slot0" / "save.bin", std::ios::binary | std::ios::trunc) << "played";
        fs::create_directories(root / "saves" / "slot1");
        std::ofstream(root / "saves" / "slot1" / "save.bin", std::ios::binary) << "first save";
        std::ofstream(root / "saves" / "online-ended.txt") << "reason=quit\n";
        std::ofstream(root / "figures" / "p1-0.sky", std::ios::binary | std::ios::trunc)
            << std::string(FigureDumpBytes, char(5));
        std::ofstream(root / "figures" / "p2-0.sky", std::ios::binary | std::ios::trunc)
            << std::string(FigureDumpBytes, char(6));
        ONLINE_EXPECT(FinishSession(root, realSaves, error));
        ONLINE_EXPECT(ReadText(realSaves / "slot0" / "save.bin") == "played");
        ONLINE_EXPECT(ReadText(realSaves / "slot1" / "save.bin") == "first save");
        ONLINE_EXPECT(!fs::exists(realSaves / "online-ended.txt") && ReadText(realSaves / "portal.txt") == realPortal);
        ONLINE_EXPECT(ReadText(scratch / "online-real-saves-backup" / "slot0" / "save.bin") == "old save");
        ONLINE_EXPECT(ReadText(collectionDir / "Spyro.sky") == std::string(FigureDumpBytes, char(5)));
        ONLINE_EXPECT(!fs::exists(root / "session.txt"));
        ONLINE_EXPECT(FinishSession(root, realSaves, error)); // nothing left to do

        // The guest: the host's save stays out of its own saves; its own figure goes back.
        plan.host = false;
        plan.hostAddress = "2001:db8::7";
        plan.localFigureFiles = { datadir::Text(collectionDir / "Spyro.sky") };
        GameLaunch guestLaunch;
        ONLINE_EXPECT(PrepareSession(*game, plan, root, guestLaunch, error));
        ONLINE_EXPECT(Variable(guestLaunch, "SKY_NETPLAY_JOIN") == "[2001:db8::7]:47641" &&
                      Variable(guestLaunch, "SKY_NETPLAY_HOST") == "<unset>");
        std::ofstream(root / "saves" / "slot0" / "save.bin", std::ios::binary | std::ios::trunc) << "guest played";
        std::ofstream(root / "figures" / "p2-0.sky", std::ios::binary | std::ios::trunc)
            << std::string(FigureDumpBytes, char(7));
        ONLINE_EXPECT(FinishSession(root, realSaves, error));
        ONLINE_EXPECT(ReadText(realSaves / "slot0" / "save.bin") == "played");
        ONLINE_EXPECT(ReadText(collectionDir / "Spyro.sky") == std::string(FigureDumpBytes, char(7)));

        // Swap Force: each player's swapper, both halves, in the two slots of its seat.
        if (const collection::Game* swap = collection::Catalogue::Get().FindGame("swapforce"))
        {
            Player swapHost = SamplePlayer("Host", 1), swapGuest = SamplePlayer("Guest", 2);
            swapHost.figures.resize(1);
            swapGuest.figures.resize(1);
            swapHost.figures[0].seat = swapGuest.figures[0].seat = 0;
            swapHost.figures[0].dump.assign(2 * FigureDumpBytes, 3);
            swapGuest.figures[0].dump.assign(2 * FigureDumpBytes, 4);
            SessionPlan swapPlan = plan;
            swapPlan.hostPlayer = swapHost;
            swapPlan.guestPlayer = swapGuest;
            swapPlan.localFigureFiles.clear();
            GameLaunch swapLaunch;
            ONLINE_EXPECT(PrepareSession(*swap, swapPlan, root, swapLaunch, error));
            const std::string swapPortal = ReadText(root / "saves" / "portal.txt");
            const auto at = [&](int slot, const char* name) {
                return swapPortal.find(std::to_string(slot) + " " + datadir::Text(root / "figures" / name) + "\n") !=
                       std::string::npos;
            };
            ONLINE_EXPECT(
                at(0, "p1-0.sky") && at(1, "p1-0-bottom.sky") && at(2, "p2-0.sky") && at(3, "p2-0-bottom.sky"));
            ONLINE_EXPECT(FinishSession(root, realSaves, error));
        }
        else
            ONLINE_EXPECT(false);

        // "Play online" in the game's F3 menu: the game's id once, then nothing; a stray file is removed too.
        const fs::path saves = scratch / "online-open-saves";
        fs::create_directories(saves);
        ONLINE_EXPECT(TakeOpenOnline(saves).empty());
        std::ofstream(saves / "open-online.txt") << "giants\n";
        ONLINE_EXPECT(TakeOpenOnline(saves) == "giants" && !fs::exists(saves / "open-online.txt"));
        ONLINE_EXPECT(TakeOpenOnline(saves).empty());
        std::ofstream(saves / "open-online.txt") << "../x\n";
        ONLINE_EXPECT(TakeOpenOnline(saves).empty() && !fs::exists(saves / "open-online.txt"));
    }
}

int OnlineChecks(const std::filesystem::path& scratch)
{
    g_failures = 0;
    CipherChecks();
    MessageChecks();
    RoomChecks();
    SecurityChecks();
    SessionChecks(scratch);
    if (g_failures == 0)
        printf("online room checks: all passed\n");
    return g_failures;
}
