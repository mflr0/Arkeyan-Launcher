#include "launcher/online/room_inputs.h"
#include "launcher/online/room_code.h"
#include "launcher/ui/online_page.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/save_backup.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include "launcher/updater.h"
#include "launcher/game_install.h"
#include "launcher/ui/collection_model.h"
#include "shared/collection/datadir.h"
#include "test_env.h"
#include <cstdio>
#include <fstream>

namespace online_ui_checks
{
    int failures = 0;
    void Expect(bool condition, const char* what)
    {
        if (!condition)
        {
            fprintf(stderr, "FAIL online inputs: %s\n", what);
            ++failures;
        }
    }
    void Write(const std::filesystem::path& file, std::string_view bytes)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out.write(bytes.data(), bytes.size());
    }
}

namespace launcher_ui
{
    struct OnlineUiChecks
    {
        static void Recovery(const std::filesystem::path& scratch)
        {
            using namespace online_ui_checks;
            namespace fs = std::filesystem;
            const fs::path data = fs::absolute(scratch);
            const fs::path session = data / "online" / "session";
            const fs::path saves = data / "own-saves";
            const fs::path figure = data / "own-figure.sky";
            const fs::path destination = data / "online" / "destination.txt";
            const fs::path endedFile = session / "saves" / "online-ended.txt";
            std::vector<std::pair<std::string, std::optional<std::string>>> variables;
            for (const char* name : { "SKYLANDERS_DATA", "SKY_LAUNCHER_DATA", "SKY_LAUNCHER_INI", "SKY_SETTINGS",
                     "SKY_SAVE_DIR", "SKY_LAUNCHER_LANGUAGE" })
            {
                const char* value = SDL_getenv_unsafe(name);
                variables.emplace_back(name, value ? std::optional<std::string>(value) : std::nullopt);
                if (std::string_view(name) == "SKY_LAUNCHER_LANGUAGE")
                {
                    TestSetEnv(name, "en");
                    continue;
                }
                const fs::path path =
                    std::string_view(name) == "SKY_LAUNCHER_INI" || std::string_view(name) == "SKY_SETTINGS"
                        ? data / "launcher.ini"
                    : std::string_view(name) == "SKY_SAVE_DIR" ? saves
                                                               : data;
                TestSetEnv(name, datadir::Text(path).c_str());
            }
            const std::string language = i18n::Current();
            i18n::SetLanguage("en");
            auto prepare = [&](bool host, const char* reason) {
                Write(saves / "slot.dat", "before");
                Write(figure, std::string(online::FigureDumpBytes, 'a'));
                online::SessionPlan plan;
                plan.host = host;
                plan.hostAddress = "192.0.2.10";
                plan.save = { { "slot.dat", { 'a', 'f', 't', 'e', 'r' } } };
                online::Figure toy{ 16, 0, 0, "Spyro", std::vector<uint8_t>(online::FigureDumpBytes, 'b') };
                (host ? plan.hostPlayer : plan.guestPlayer).figures.push_back(toy);
                plan.localFigureFiles.push_back(datadir::Text(figure));
                GameLaunch launch;
                std::string error;
                Expect(
                    online::PrepareSession(*collection::Catalogue::Get().FindGame("ssa"), plan, session, launch, error),
                    "session recovery fixture");
                Write(destination, datadir::Text(saves) + "\nssa\nKaos\n");
                if (reason)
                    Write(endedFile, std::string("reason=") + reason + "\r\n");
            };
            auto recovered = [&](bool host) {
                Expect(savebackup::ReadFile(saves / "slot.dat") == (host ? "after" : "before"),
                    "host saves recovered, guest saves preserved");
                Expect(savebackup::ReadFile(figure) == std::string(online::FigureDumpBytes, 'b'),
                    "local figure recovered");
                Expect(!fs::exists(session / "session.txt") && !fs::exists(destination) && !fs::exists(endedFile),
                    "successful recovery consumes manifest, destination and end reason");
            };
            for (const char* reason : { "quit", "timeout", "desync", "transport", "protocol", "game", "image",
                     "runtime", "platform", "fps", "seats", "invalid", "unknown", "" })
            {
                prepare(true, *reason ? reason : nullptr);
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                recovered(true);
                const std::string text = OnlineEndText(reason);
                const bool normal = !*reason || std::string_view(reason) == "quit";
                Expect(app.m_toasts.Items().size() == (normal ? 0u : 1u),
                    "startup shows one abnormal end toast and no normal end toast");
                if (!normal && !app.m_toasts.Items().empty())
                    Expect(app.m_toasts.Items().front().text == text && !text.ends_with('.') &&
                               app.m_toasts.Items().front().kind == ui::ToastKind::Error,
                        "end reason selects an error toast without a final full stop");
                app.m_toasts.Clear();
                app.RecoverOnline();
                Expect(app.m_toasts.Items().empty(), "recovery does not repeat an end toast");
            }
            prepare(false, "quit");
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                recovered(false);
                Expect(app.m_toasts.Items().empty(), "guest normal end is silent");
            }
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                prepare(true, "fps");
                GameLaunch launch;
                fs::path runtime = fs::path(SDL_GetBasePath()) / "ArkeyanLauncherTest";
#if defined(_WIN32)
                runtime += ".exe";
#endif
                launch.runtime = datadir::Text(runtime);
                std::string error;
                const bool started = app.m_session.Start("ssa", launch, "--identity-runtime", error);
                Expect(started, "stand-in room game starts");
                if (started)
                {
                    app.m_online.launched = true;
                    const uint64_t deadline = SDL_GetTicks() + 5000;
                    while (!app.m_session.Running().empty() && SDL_GetTicks() < deadline)
                    {
                        app.PollGame();
                        SDL_Delay(1);
                    }
                    recovered(true);
                    Expect(app.m_session.Running().empty() && !app.m_online.launched &&
                               app.m_toasts.Items().size() == 1 &&
                               app.m_toasts.Items().front().text == OnlineEndText("fps"),
                        "owned game exit recovers files and shows the end reason once");
                }
            }
            prepare(true, "desync");
            savebackup::SetGameRunning(true, session / "saves");
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Expect(app.m_online.recovering && app.m_toasts.Items().empty() && fs::exists(destination) &&
                           savebackup::ReadFile(saves / "slot.dat") == "before",
                    "startup defers recovery and notification while the game runs");
                app.m_session.Poll(SDL_GetTicks());
                Expect(app.m_session.Running() == "ssa", "session marker is watched by GameSession");
                savebackup::SetGameRunning(false, session / "saves");
                SDL_Delay(GameSession::MarkerMs + 1);
                app.PollGame();
                recovered(true);
                Expect(!app.m_online.recovering && app.m_session.Running().empty() &&
                           app.m_toasts.Items().size() == 1 &&
                           app.m_toasts.Items().front().text == OnlineEndText("desync"),
                    "GameSession::Poll end recovers files and shows the end reason");
            }
            prepare(true, "timeout");
            fs::remove_all(saves);
            Write(saves, "blocked destination");
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                app.CloseOnline();
                Expect(app.m_online.recovering && !app.m_online.error.empty() && fs::exists(destination) &&
                           fs::exists(session / "session.txt") && fs::exists(endedFile),
                    "failed recovery remains available after closing the room");
                fs::remove(saves);
                app.RecoverOnline();
                recovered(true);
                Expect(!app.m_online.recovering && app.m_toasts.Items().size() == 1,
                    "retry recovers once after the destination becomes writable");
            }
            i18n::SetLanguage("fr");
            Expect(OnlineEndText("timeout") ==
                       "L'autre joueur ne répond plus\u00a0; vérifiez votre connexion et reconnectez-vous",
                "end reason is translated with vous");
            prepare(true, "timeout");
            i18n::SetLanguage("en");
            TestSetEnv("SKY_LAUNCHER_LANGUAGE", "fr");
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Expect(app.m_toasts.Items().size() == 1 &&
                           app.m_toasts.Items().front().text ==
                               "L'autre joueur ne répond plus\u00a0; vérifiez votre connexion et reconnectez-vous",
                    "startup applies the launcher's language before making the end toast");
            }
            i18n::SetLanguage(language);
            for (const auto& [name, value] : variables)
                if (value)
                    TestSetEnv(name.c_str(), value->c_str());
                else
                    TestUnsetEnv(name.c_str());
        }
    };
}

int OnlineUiChecks(const std::filesystem::path& scratch)
{
    using namespace online_ui_checks;
    namespace fs = std::filesystem;
    failures = 0;
    fs::create_directories(scratch);
    const char* original = SDL_getenv_unsafe("SKY_COLLECTION");
    const bool hadCollection = original != nullptr;
    const std::string oldCollection = original ? original : "";
    const std::string folder = datadir::Text(scratch / "collection");
    TestSetEnv("SKY_COLLECTION", folder.c_str());
    const auto* game = collection::Catalogue::Get().FindGame("ssa");
    Expect(game != nullptr, "catalogue game");
    if (!game)
        return failures;
    const std::string p1 = collection::Create(16, 0, "first.sky");
    const std::string p2 = collection::Create(7, 0, "second.sky");
    const std::string item = collection::Create(200, 0, "item.sky");
    Expect(!p1.empty() && !p2.empty() && !item.empty(), "stand-in figures");
    std::vector<std::string> seats(game->seats.size());
    for (size_t i = 0; i < seats.size(); ++i)
        seats[i] = game->seats[i].player == 1 ? p1 : game->seats[i].player == 2 ? p2 : item;
    const std::string portal = datadir::Text(scratch / "portal.txt");
    Expect(launcher_ui::WriteSeats(*game, portal, seats), "seat fixture");
    const fs::path saves = scratch / "saves";
    Write(saves / "slot" / "GLOBAL.DAT", std::string("save\0bytes", 10));
    Write(saves / "empty.dat", "");
    Write(saves / "portal.txt", "excluded");
    Write(saves / "backups" / "old.dat", "excluded");
    Write(saves / "slot" / "old.bak", "excluded");
    Write(scratch / "runtime", "stand-in");
    LauncherConfig config;
    const std::string sha(64, 'a');
    RecordInstalledGame(config, sha, datadir::Text(scratch / "runtime"), "test", "1.2.3");
    config["ssa.video"]["fps_limit"] = "60";
    config["ssa"]["language"] = "fr";
    config["ssa"]["voice_language"] = "en";
    config["ssa"]["nightmare_from_start"] = "1";
    profiles::Profile profile;
    profile.id = "first";
    profile.name = "Spyro";
    profile.xuid = "E000000000000001";
    profile.color = 4;
    auto host = online::ReadRoomInputs(config, profile, *game, sha, true, portal, saves);
    Expect(host.error == online::InputError::None, "host inputs readable");
    Expect(host.hello.launcherVersion == LauncherVersion() && host.hello.gameId == "ssa" &&
               host.hello.xexSha256 == sha && host.hello.gameVersion == "1.2.3" && !host.hello.crossPlatform &&
               host.hello.platform == online::RoomPlatform(),
        "compatibility hello");
    Expect(host.player.pseudo == "Spyro" && host.player.xuid == profile.xuid && host.player.color == 4,
        "active profile identity");
    Expect(host.settings.fpsLimit == 60 && host.settings.language == "fr" && host.settings.voices == "en" &&
               host.settings.nightmareFromStart,
        "host launch settings");
    Expect(host.save.size() == 2 && host.settings.saveBytes == 10 && host.settings.saveFiles == 2 &&
               host.save[1].path == "slot/GLOBAL.DAT" && host.save[1].bytes[4] == 0,
        "relative binary saves, empty file, exclusions");
    Expect(host.player.figures.size() == 2 && host.localFigureFiles.size() == 2,
        "host brings player one and shared figures only");
    for (size_t i = 0; i < host.player.figures.size(); ++i)
    {
        Expect(host.player.figures[i].dump.size() == online::FigureDumpBytes && host.localFigureFiles[i] != p2 &&
                   game->seats[host.player.figures[i].seat].player != 2,
            "figure bytes and write-back paths match");
    }
    config["ssa.online"]["cross_platform"] = "1";
    auto guest = online::ReadRoomInputs(config, profile, *game, sha, false, portal, saves);
    Expect(guest.hello.crossPlatform && guest.save.empty() && guest.player.figures.size() == 1 &&
               guest.localFigureFiles[0] == p1,
        "guest brings own player one, no shared figures or saves");
    {
        LauncherConfig fresh;
        const auto none = online::ReadGameOnline(fresh, "ssa");
        Expect(none.mode == online::Mode::Solo && none.port == online::DefaultPort && none.automatic &&
                   none.address.empty() && !none.crossPlatform,
            "a game never set up plays alone");
        online::GameOnline set{ online::Mode::Join, 50000, false, "203.0.113.4:50000", "pw", true };
        online::WriteGameOnline(fresh, "giants", set);
        const auto back = online::ReadGameOnline(fresh, "giants");
        Expect(back.mode == online::Mode::Join && back.port == 50000 && !back.automatic &&
                   back.address == set.address && back.password == "pw" && back.crossPlatform &&
                   online::ReadGameOnline(fresh, "ssa").mode == online::Mode::Solo,
            "online settings are kept per game");
        Expect(online::VersionLabel("0.10.0", true) == "0.10.0-o" &&
                   online::VersionLabel("0.10.0", false) == "0.10.0" && online::VersionLabel("", true).empty(),
            "a game set to play online shows -o after its version");
        fresh["ssa.online"]["port"] = "70000";
        fresh["ssa.online"]["mode"] = "host";
        Expect(online::ReadGameOnline(fresh, "ssa").port == online::DefaultPort &&
                   online::ReadGameOnline(fresh, "ssa").mode == online::Mode::Host,
            "a port out of range falls back to the default");
    }
    config["ssa.video"]["fps_limit"] = "30";
    Expect(online::RoomSettings(config, "ssa").fpsLimit == 30, "original frame rate");
    const auto* swap = collection::Catalogue::Get().FindGame("swapforce");
    const auto halves = collection::CreateSwapper(2000, 1000);
    Expect(swap && !halves.top.empty() && !halves.bottom.empty(), "stand-in swapper halves");
    if (swap && !halves.top.empty())
    {
        std::vector<std::string> swapSeats(swap->seats.size());
        for (size_t i = 0; i < swapSeats.size(); ++i)
            if (swap->seats[i].player == 1)
            {
                swapSeats[i] = halves.top;
                break;
            }
        const auto swapPortal = datadir::Text(scratch / "swap-portal.txt");
        Expect(launcher_ui::WriteSeats(*swap, swapPortal, swapSeats), "swapper seat fixture");
        const auto inputs = online::ReadRoomInputs(config, profile, *swap, sha, false, swapPortal, saves);
        Expect(inputs.error == online::InputError::None && inputs.player.figures.size() == 2 &&
                   inputs.localFigureFiles == std::vector<std::string>{ halves.top, halves.bottom } &&
                   inputs.player.figures[0].seat == inputs.player.figures[1].seat,
            "both swapper halves keep the player's seat and own write-back files");
        online::Player decoded;
        Expect(online::Decode(online::Encode(inputs.player), decoded) && decoded.figures.size() == 2,
            "swapper room description survives the protocol");
    }
    profile.id = "room-picture";
    profile.hasPicture = true;
    auto badPicture = online::ReadRoomInputs(config, profile, *game, sha, false, portal, saves);
    Expect(badPicture.error == online::InputError::Picture, "unreadable profile picture refused");
    Write(profiles::PictureOf(profile), "stand-in PNG bytes");
    auto picture = online::ReadRoomInputs(config, profile, *game, sha, false, portal, saves);
    Expect(picture.error == online::InputError::None && picture.player.picture.size() == 18,
        "profile picture bytes included");
    fs::remove(profiles::PictureOf(profile));
    profile.hasPicture = false;
    StoreDiscLanguages(config, "ssa", { { "fr", "en" }, { "fr" } });
    config["ssa"]["language"] = "de";
    config["ssa"]["voice_language"] = "de";
    const auto languages = online::RoomSettings(config, "ssa");
    Expect(
        languages.language == "fr" && languages.voices.empty(), "languages absent from the disc use launch fallbacks");
    profiles::Profile unnamed = profile;
    unnamed.name.clear();
    auto first = online::ReadRoomInputs(config, unnamed, *game, sha, false, portal, saves);
    Expect(!first.player.pseudo.empty(), "unnamed first profile has a usable room name");
    fs::resize_file(saves / "empty.dat", 64 * 1024 * 1024 + 1);
    auto large = online::ReadRoomInputs(config, profile, *game, sha, true, portal, saves);
    Expect(large.error == online::InputError::SaveTooLarge, "saves over 64 MB refused before reading");
    fs::remove(saves / "empty.dat");
    Write(saves / "outside-link.dat", "");
    fs::remove(saves / "outside-link.dat");
    std::error_code linkError;
    fs::create_symlink(scratch / "runtime", saves / "outside-link.dat", linkError);
    // Wine makes a Unix link that its own file system does not report as a link: nothing to refuse there.
    if (!linkError && fs::is_symlink(saves / "outside-link.dat", linkError))
    {
        auto linked = online::ReadRoomInputs(config, profile, *game, sha, true, portal, saves);
        Expect(linked.error == online::InputError::Save, "save symlinks refused");
        fs::remove(saves / "outside-link.dat");
    }
    auto missing = online::ReadRoomInputs(config, profile, *game, sha, true, portal, scratch / "absent");
    Expect(missing.error == online::InputError::None && missing.save.empty(), "first game has no saves");
    if (hadCollection)
        TestSetEnv("SKY_COLLECTION", oldCollection.c_str());
    else
        TestUnsetEnv("SKY_COLLECTION");
    for (int value = 1; value <= int(online::Error::Broken); ++value)
        Expect(*launcher_ui::OnlineErrorText(online::Error(value), online::Mismatch::None) != '\0',
            "room error has a sentence");
    for (int value = 0; value <= int(online::Mismatch::Platform); ++value)
        Expect(*launcher_ui::OnlineErrorText(online::Error::Mismatch, online::Mismatch(value)) != '\0',
            "mismatch has a sentence");
    {
        online::Hello mine, theirs;
        mine.gameVersion = "0.9.16";
        theirs.gameVersion = "0.9.15";
        mine.platform = "macos-arm64";
        theirs.platform = "windows-x64";
        for (int value = 1; value <= int(online::Error::Broken); ++value)
        {
            const auto help = launcher_ui::OnlineHelpFor(online::Error(value), online::Mismatch::None, mine, theirs);
            Expect(value == int(online::Error::Mismatch) || (!help.title.empty() && !help.lines.empty()),
                "every room error has a help card");
        }
        for (int value = 0; value <= int(online::Mismatch::Platform); ++value)
        {
            const auto help =
                launcher_ui::OnlineHelpFor(online::Error::Mismatch, online::Mismatch(value), mine, theirs);
            Expect(!help.title.empty() && !help.lines.empty(), "every mismatch has a help card");
        }
        using Action = launcher_ui::OnlineHelp::Action;
        const auto versions =
            launcher_ui::OnlineHelpFor(online::Error::Mismatch, online::Mismatch::GameVersion, mine, theirs);
        Expect(versions.lines.size() >= 3 && versions.lines[0].find("0.9.16") != std::string::npos &&
                   versions.lines[1].find("0.9.15") != std::string::npos && versions.action == Action::Update,
            "a version mismatch shows both versions and leads to the update");
        const auto systems =
            launcher_ui::OnlineHelpFor(online::Error::Mismatch, online::Mismatch::Platform, mine, theirs);
        Expect(systems.lines[0].find("macOS") != std::string::npos &&
                   systems.lines[1].find("Windows") != std::string::npos && systems.action == Action::Settings,
            "a system mismatch names both systems and leads to the Online tab");
        Expect(launcher_ui::OnlineHelpFor(online::Error::CannotConnect, online::Mismatch::None, mine, theirs).action ==
                   Action::Retry,
            "an unanswered join can be tried again");
        Expect(launcher_ui::MaskedAddress("203.0.113.4:47640") == "********:47640" &&
                   launcher_ui::MaskedAddress("203.0.113.4").find("203") == std::string::npos,
            "a masked address hides the IP and keeps the port");
    }
    auto wait = [](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!condition() && std::chrono::steady_clock::now() < deadline)
            SDL_Delay(1);
        return condition();
    };
    std::atomic<int> maps = 0, unmaps = 0, lookups = 0;
    std::atomic<bool> bothProtocols = true;
    {
        launcher_ui::RouterLease lease(
            [&](const netplay::portmap::Options& options) {
                bothProtocols = bothProtocols && options.alsoTcp && options.port == 47640;
                ++maps;
                netplay::portmap::Mapping mapping;
                mapping.ok = true;
                mapping.externalIp = "192.0.2.10";
                mapping.externalPort = options.port;
                return mapping;
            },
            [&](const auto&, const auto&) { ++unmaps; }, std::chrono::milliseconds(20),
            [&] {
                ++lookups;
                return online::stun::Result{ { "198.51.100.7", 0 }, { "2001:db8::7", 0 } };
            });
        lease.Open(47640);
        Expect(wait([&] { return maps >= 2 && lease.Get().ok; }), "router lease is renewed on its worker");
        Expect(lookups == 1 && lease.Seen().v4.ip == "198.51.100.7" && lease.Seen().v6.ip == "2001:db8::7",
            "the public addresses are asked once per room, not at each renewal");
        lease.Close();
        Expect(wait([&] { return unmaps == 1; }), "closing room removes the lease");
    }
    Expect(bothProtocols, "router maps both TCP and UDP");
    std::mutex gateMutex;
    std::condition_variable gate;
    bool entered = false, release = false;
    unmaps = 0;
    {
        launcher_ui::RouterLease lease(
            [&](const auto&) {
                std::unique_lock lock(gateMutex);
                entered = true;
                gate.notify_one();
                gate.wait(lock, [&] { return release; });
                netplay::portmap::Mapping mapping;
                mapping.ok = true;
                return mapping;
            },
            [&](const auto&, const auto&) { ++unmaps; }, std::chrono::hours(1),
            [&] {
                ++lookups;
                return online::stun::Result{};
            });
        lookups = 0;
        lease.Open(47640);
        {
            std::unique_lock lock(gateMutex);
            Expect(gate.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }),
                "router request runs off the window thread");
        }
        lease.Close();
        {
            std::lock_guard lock(gateMutex);
            release = true;
        }
        gate.notify_one();
        Expect(wait([&] { return unmaps == 1; }), "late router result is removed after leaving");
        Expect(!lease.Get().ok && !lease.Pending(), "late result cannot reopen the room");
        Expect(lookups == 0, "a room closed while the router answers asks no public address");
    }
    {
        maps = 0;
        launcher_ui::RouterLease lease(
            [&](const auto&) {
                ++maps;
                return netplay::portmap::Mapping{};
            },
            [](const auto&, const auto&) {}, std::chrono::hours(1),
            [] { return online::stun::Result{ { "198.51.100.7", 0 }, {} }; });
        lease.Open(47640, false);
        Expect(wait([&] { return !lease.Pending() && lease.Seen().v4.ip == "198.51.100.7"; }) && maps == 0,
            "with automatic opening off, the router is not asked but the public address is");
    }
    {
        using Reach = launcher_ui::Invite::Reach;
        using netplay::portmap::Mapping;
        const online::stun::Result none, v4{ { "198.51.100.7", 0 }, {} },
            both{ { "198.51.100.7", 0 }, { "2001:db8::7", 0 } };
        const Mapping opened{ .ok = true, .externalIp = "203.0.113.4", .externalPort = 47640 };
        const Mapping shared{ .ok = true, .externalIp = "100.64.0.9", .externalPort = 47640 };
        std::string host;
        uint16_t port = 0;
        auto holds = [&](const launcher_ui::Invite& invite, const char* ip, uint16_t to) {
            return online::DecodeRoomCode(invite.code, host, port) && host == ip && port == to;
        };
        auto invite = launcher_ui::InviteFor(opened, both, true, true, "192.168.1.20", 47640);
        Expect(invite.reach == Reach::Asking && invite.code.empty(), "no room code while the router is asked");
        invite = launcher_ui::InviteFor(opened, both, false, true, "192.168.1.20", 47640);
        Expect(invite.reach == Reach::Anywhere && holds(invite, "203.0.113.4", 47640) &&
                   invite.address == "203.0.113.4:47640",
            "an opened router: the code holds its public address");
        Expect(online::DecodeRoomCode(invite.homeCode, host, port) && host == "192.168.1.20",
            "the code for a friend at home holds the local address");
        invite = launcher_ui::InviteFor({}, both, false, true, "192.168.1.20", 47640);
        Expect(invite.reach == Reach::Ipv6 && holds(invite, "2001:db8::7", 47640) &&
                   invite.address == "[2001:db8::7]:47640",
            "a refusing router with IPv6: the code holds the IPv6 address");
        invite = launcher_ui::InviteFor(shared, v4, false, true, "192.168.1.20", 47640);
        Expect(invite.reach == Reach::Shared && holds(invite, "198.51.100.7", 47640),
            "an address shared by the provider is told apart");
        invite = launcher_ui::InviteFor({}, v4, false, true, "192.168.1.20", 47640);
        Expect(invite.reach == Reach::Refused && holds(invite, "198.51.100.7", 47640), "a refusing router");
        invite = launcher_ui::InviteFor({}, both, false, false, "192.168.1.20", 47641);
        Expect(invite.reach == Reach::Manual && holds(invite, "198.51.100.7", 47641),
            "automatic opening off: the public IPv4 address, for a forwarded port");
        invite = launcher_ui::InviteFor({}, none, false, true, "192.168.1.20", 47640);
        Expect(invite.reach == Reach::Refused && holds(invite, "192.168.1.20", 47640),
            "no public address known: the local one");
        Expect(launcher_ui::MaskedCode("K7QD-M2XA") == "****-****", "a masked code keeps only its dashes");
    }
    launcher_ui::OnlineUiChecks::Recovery(scratch / "recovery");
    fprintf(stderr, "Online UI inputs, session recovery and router worker: %d failure(s)\n", failures);
    return failures;
}
