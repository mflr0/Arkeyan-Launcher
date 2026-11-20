// ArkeyanLauncherTest's checks for the online room (launcher/online/): the messages and their limits, the keyed tags,
// addresses, then whole rooms over the loopback address: a guest that joins, gets the host's settings and save, and both
// start; a wrong password, another disc, a full room, a guest that leaves, a silent one. Returns the number of failures.
#include "launcher/online/lobby.h"
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
        std::vector<uint8_t> future = { 2, 0, 0xFF };
        ONLINE_EXPECT(Decode(future, back) && back.protocol == 2);

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
        const Keys k1 = DeriveKeys("pw", n1, n2), k2 = DeriveKeys("pw", n1, n2), k3 = DeriveKeys("pW", n1, n2),
                   k4 = DeriveKeys("pw", n2, n1);
        ONLINE_EXPECT(k1.hostToGuest == k2.hostToGuest && k1.hostToGuest != k3.hostToGuest &&
                      k1.hostToGuest != k4.hostToGuest && k1.hostToGuest != k1.guestToHost);
        ONLINE_EXPECT(ProofOf(k1, true) != ProofOf(k1, false));

        // Frames: whole ones out of bytes fed one at a time; the tag binds type, sequence and payload.
        const std::vector<uint8_t> payload = { 1, 2, 3 };
        const auto frame = Frame(MessageType::Ready, 7, payload, &k1.hostToGuest);
        FrameReader reader;
        int count = 0;
        for (const uint8_t byte : frame)
        {
            reader.Feed(std::span(&byte, 1));
            while (auto got = reader.Next())
            {
                ++count;
                ONLINE_EXPECT(got->type == MessageType::Ready && got->sequence == 7 && got->payload == payload);
                ONLINE_EXPECT(CheckTag(*got, k1.hostToGuest) && !CheckTag(*got, k1.guestToHost));
                got->payload[0] ^= 1;
                ONLINE_EXPECT(!CheckTag(*got, k1.hostToGuest));
                got->payload[0] ^= 1;
                got->sequence++;
                ONLINE_EXPECT(!CheckTag(*got, k1.hostToGuest));
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
    }

    // Two rooms' worth of polling on one thread, on a clock the test moves.
    struct Room
    {
        uint64_t now = 1000;
        Lobby host{ [this] { return now; } };
        Lobby guest{ [this] { return now; } };
        uint16_t port = 0;

        bool Open(const std::string& password, std::vector<SaveFile> save = {})
        {
            HostOptions options;
            options.password = password;
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

        void Join(Lobby& who, const std::string& password, Hello hello = SampleHello())
        {
            JoinOptions options;
            options.address = "127.0.0.1:" + std::to_string(port);
            options.password = password;
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

    void RoomChecks()
    {
        {
            std::vector<SaveFile> save = { { "slot0/save.bin", std::vector<uint8_t>(150000) },
                { "options.cfg", { 1, 2 } } };
            for (size_t i = 0; i < save[0].bytes.size(); ++i)
                save[0].bytes[i] = uint8_t(i * 7);
            Room room;
            ONLINE_EXPECT(room.Open("secret", save) && room.host.Get().state == State::Waiting);
            room.Join(room.guest, "secret");
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
            ONLINE_EXPECT(room.Open("secret"));
            room.Join(room.guest, "wrong");
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Ended; }));
            ONLINE_EXPECT(room.guest.Get().error == Error::WrongPassword);
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().lastRefusal.error == Error::WrongPassword; }));
            ONLINE_EXPECT(room.host.Get().state == State::Waiting);
            // The room stays open: the right password gets in after a wrong one.
            room.Join(room.guest, "secret");
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open(""));
            Hello other = SampleHello();
            other.xexSha256 = std::string(64, 'b');
            room.Join(room.guest, "", other);
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::Ended; }));
            ONLINE_EXPECT(room.guest.Get().error == Error::Mismatch && room.guest.Get().mismatch == Mismatch::Disc);
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().lastRefusal.mismatch == Mismatch::Disc; }));
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open(""));
            room.Join(room.guest, "");
            ONLINE_EXPECT(room.Until([&] { return room.guest.Get().state == State::InRoom; }));
            Lobby third([&room] { return room.now; });
            room.Join(third, "");
            ONLINE_EXPECT(room.Until([&] { return third.Get().state == State::Ended; }, &third));
            ONLINE_EXPECT(third.Get().error == Error::RoomFull && room.guest.Get().state == State::InRoom);
            room.guest.Leave();
            ONLINE_EXPECT(room.Until([&] { return room.host.Get().state == State::Waiting; }));
            ONLINE_EXPECT(room.host.Get().lastRefusal.error == Error::Left);
        }
        {
            Room room;
            ONLINE_EXPECT(room.Open(""));
            room.Join(room.guest, "");
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
    MessageChecks();
    RoomChecks();
    SessionChecks(scratch);
    if (g_failures == 0)
        printf("online room checks: all passed\n");
    return g_failures;
}
