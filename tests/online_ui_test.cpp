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
#include "datagram_fake.h"
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
        struct Context
        {
            Context() { ImGui::CreateContext(); }
            ~Context() { ImGui::DestroyContext(); }
        };

        struct Room
        {
            App& app;
            uint64_t now = 1000;
            std::shared_ptr<online_tests::DatagramFake> io = std::make_shared<online_tests::DatagramFake>();
            online::Lobby peer{ [this] { return now; }, io };
            explicit Room(App& owner) : app(owner)
            {
                std::destroy_at(&app.m_online.lobby);
                std::construct_at(&app.m_online.lobby, [this] { return now; }, io);
                app.m_shots = "unused";
            }
            ~Room()
            {
                app.CloseOnline();
                std::destroy_at(&app.m_online.lobby);
                std::construct_at(&app.m_online.lobby);
            }
            bool Until(const std::function<bool()>& done, bool ui = false)
            {
                for (int i = 0; i < 3000; ++i, now += 5)
                {
                    if (ui)
                        app.PollOnline();
                    else
                        app.m_online.lobby.Poll();
                    peer.Poll();
                    if (done())
                        return true;
                }
                return false;
            }
            void Connect(bool hosting)
            {
                using namespace online_ui_checks;
                app.CloseOnline();
                peer.Leave();
                io = std::make_shared<online_tests::DatagramFake>();
                std::destroy_at(&app.m_online.lobby);
                std::construct_at(&app.m_online.lobby, [this] { return now; }, io);
                std::destroy_at(&peer);
                std::construct_at(&peer, [this] { return now; }, io);
                auto& room = app.m_online;
                const auto* game = collection::Catalogue::Get().FindGame("ssa");
                room.game = "ssa";
                room.realSaves = FoldersOf("ssa").saves;
                room.inputs = online::ReadRoomInputs(app.m_config, app.m_profiles.Active(), *game, std::string(64, 'a'),
                    hosting, SessionFile(*game), room.realSaves);
                online::HostOptions host;
                host.port = 47740;
                host.discover = false;
                host.hello = room.inputs.hello;
                host.player = room.inputs.player;
                host.settings = room.inputs.settings;
                Expect((hosting ? room.lobby : peer).Host(host), "room bugs host fixture");
                online::JoinOptions join;
                join.discover = false;
                join.address = online::EncodeRoomCode(std::array{ online::net::Endpoint{ "127.0.0.1", host.port } },
                    (hosting ? room.lobby : peer).Secret());
                join.hello = host.hello;
                join.player = host.player;
                Expect((hosting ? peer : room.lobby).Join(join), "room bugs guest fixture");
                Expect(Until([&] { return room.lobby.Get().haveRemote && peer.Get().haveRemote; }),
                    "room bugs connected fixture");
                app.m_page = Page::Online;
            }
        };

        static void RoomBugs(const std::filesystem::path& data)
        {
            TestSetEnv("SKY_LAUNCHER_LANGUAGE", "en");
            i18n::SetLanguage("en");
            using namespace online_ui_checks;
            namespace fs = std::filesystem;
            const auto* game = collection::Catalogue::Get().FindGame("ssa");
            const std::string figure = collection::Create(16, 0, "room-bugs.sky");
            std::vector<std::string> seats(game->seats.size());
            seats[0] = figure;
            Expect(WriteSeats(*game, SessionFile(*game), seats), "room bugs portal fixture");
            const auto disc = data / "stand-in.iso";
            Write(disc, "stand-in");
            auto ready = [&](App& app) {
                app.m_s->started = true;
                app.m_s->disc = DiscState::Ready;
                app.m_s->check.xexSha256 = std::string(64, 'a');
                app.m_s->discPath = datadir::Text(disc);
            };
            for (bool hosting : { true, false })
            {
                LauncherConfig config;
                config["ssa.online"]["mode"] = hosting ? "host" : "join";
                config["ssa.online"]["port"] = "47741";
                config["ssa.online"]["address"] =
                    online::EncodeRoomCode(std::array{ online::net::Endpoint{ "192.0.2.10", 47741 } }, 0x1234567890);
                const auto session = data / "online" / "session";
                auto prepare = [&](bool replay) {
                    online::SessionPlan plan;
                    plan.host = hosting;
                    plan.hostAddress = "192.0.2.10";
                    GameLaunch launch;
                    std::string error;
                    Expect(online::PrepareSession(*game, plan, session, launch, error), "room replay fixture");
                    Write(data / "online" / "destination.txt", datadir::Text(FoldersOf("ssa").saves) + "\nssa\n\n");
                    if (replay)
                    {
                        Write(session / "saves" / "online-ended.txt", "reason=quit\n");
                        Write(session / "saves" / "online-replay.txt", "replay\n");
                    }
                };
                prepare(true);
                {
                    App app(*FindGame("ssa"), LauncherConfigPath(), config);
                    Context context;
                    Room room(app);
                    app.m_s->disc = DiscState::Verifying;
                    app.PollOnline();
                    Expect(app.m_online.lobby.Get().state == online::State::Idle && app.m_page == Page::Online,
                        "bug 2: restart waits for disc verification without joining an empty code");
                    ready(app);
                    app.PollOnline();
                    Expect(std::string(app.m_online.port) == "47741" &&
                               std::string(app.m_online.address) == config["ssa.online"]["address"] &&
                               app.m_online.inputs.hello.xexSha256 == std::string(64, 'a') &&
                               app.m_online.lobby.Get().state ==
                                   (hosting ? online::State::Waiting : online::State::Connecting),
                        "bug 2: verified restart uses saved port, code and disc edition");
                }
                for (bool replay : { true, false })
                {
                    prepare(replay);
                    savebackup::SetGameRunning(true, session / "saves");
                    App app(*FindGame("ssa"), LauncherConfigPath(), config);
                    Context context;
                    Room room(app);
                    ready(app);
                    SaveLauncherConfig(app.m_configPath, config);
                    app.m_session.Poll(SDL_GetTicks());
                    app.m_online.launched = true;
                    savebackup::SetGameRunning(false, session / "saves");
                    SDL_Delay(GameSession::MarkerMs + 1);
                    app.PollGame();
                    app.PollOnline();
                    Expect(!app.m_online.launched && app.m_page == Page::Online &&
                               (replay ? app.m_online.lobby.Get().state ==
                                             (hosting ? online::State::Waiting : online::State::Connecting)
                                       : !app.m_online.error.empty()),
                        "bug 1: PollGame keeps the replay room or crash recovery message");
                }
            }
            for (bool hosting : { true, false })
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Context context;
                Room room(app);
                room.Connect(hosting);
                for (Page source : { Page::Online, Page::Collection })
                    for (Page destination :
                        { Page::Home, Page::Play, Page::Collection, Page::Settings, Page::GameSettings, Page::Online })
                    {
                        app.m_page = source;
                        app.GoTo(destination);
                        Expect(
                            app.RoomOpen() && room.peer.Get().haveRemote, "navigation preserves host and guest rooms");
                    }
                app.m_page = Page::Online;
                app.Back();
                Expect(app.RoomOpen() && app.m_page == Page::Play, "Back keeps the room");
                app.StepMainPage(1);
                Expect(app.RoomOpen(), "LB/RB and game selection keep the room");
                app.OpenRoomPage();
                Expect(app.m_s->game.id == app.m_online.game && app.m_page == Page::Online,
                    "sidebar returns to the room game");
                app.m_page = Page::Collection;
                app.PollOnline();
                Expect(!app.m_toasts.Items().empty() && app.m_toasts.Items().back().text.ends_with(" joined the room"),
                    "friend joins while browsing: toast");
                app.m_toasts.Clear();
                room.peer.SetReady(true);
                Expect(room.Until([&] { return app.m_online.lobby.Get().remoteReady; }, true), "friend ready fixture");
                Expect(app.m_toasts.Items().size() == 1 && app.m_toasts.Items().front().text.ends_with(" is ready"),
                    "friend ready while browsing: one toast");
                app.PollOnline();
                Expect(app.m_toasts.Items().size() == 1, "unchanged readiness does not repeat toast");
                app.OpenRoomPicker();
                Expect(app.m_online.picker && app.m_page == Page::Collection, "picker opens without navigation");
                const std::string replacement = collection::Create(7, 0, "room-replacement.sky");
                Expect(app.SetRoomSeat(0, replacement), "picker writes own player seat");
                Expect(ReadSeats(*game, SessionFile(*game))[0] == replacement, "picker persists Collection seats");
                app.PollOnline();
                Expect(app.m_online.inputs.localFigureFiles.front() == replacement,
                    "room inputs refresh from picker seats");
                const std::string item = collection::Create(200, 0, "room-shared.sky");
                app.m_roomFigures.Load();
                for (size_t seat = 0; seat < game->seats.size(); ++seat)
                {
                    const auto& definition = game->seats[seat];
                    if (definition.player == 0 && SeatAccepts(definition, *app.m_roomFigures.Find(item)))
                        Expect(app.SetRoomSeat(seat, item) == hosting, "only host can write shared item seat");
                    if (definition.player == 2)
                        Expect(!app.SetRoomSeat(seat, replacement), "picker cannot write the other player's seat");
                }
                ready(app);
                app.m_s->installed.runtime = "display-only";
                app.m_autoPlay = true;
                app.AutoPlay();
                Expect(bool(app.m_afterRoomLeave) && !app.m_autoPlay && app.RoomOpen(),
                    "AutoPlay and shortcut requests ask before leaving");
                app.ResolveRoomLeave(false);
                app.PlayNow();
                Expect(bool(app.m_afterRoomLeave) && app.RoomOpen() && app.m_session.Running().empty(),
                    "solo play asks before leaving or launching");
                app.ResolveRoomLeave(false);
                Expect(app.RoomOpen() && !app.m_afterRoomLeave, "Stay cancels solo play");
                bool continued = false;
                app.ConfirmRoomLeave([&] { continued = !app.RoomOpen(); });
                app.ResolveRoomLeave(true);
                Expect(continued && !app.RoomOpen(), "Leave closes the room before continuing");
                room.Connect(hosting);
                app.RequestQuit();
                Expect(bool(app.m_afterRoomLeave) && app.m_running, "window close with friend asks first");
                app.ResolveRoomLeave(false);
                Expect(app.RoomOpen() && app.m_running, "Stay cancels window close");
                app.RequestQuit();
                app.ResolveRoomLeave(true);
                Expect(!app.RoomOpen() && !app.m_running, "Leave confirms window close");
                app.m_running = true;
                room.Connect(hosting);
                app.m_page = Page::Collection;
                app.m_toasts.Clear();
                app.PollOnline();
                app.m_toasts.Clear();
                room.peer.Leave();
                Expect(room.Until(
                           [&] {
                               return !app.m_online.lobby.Get().haveRemote ||
                                      app.m_online.lobby.Get().state == online::State::Ended;
                           },
                           true),
                    "friend leaves fixture");
                if (hosting)
                {
                    Expect(app.RoomOpen() && !app.m_toasts.Items().empty() &&
                               app.m_toasts.Items().back().text.ends_with(" left the room"),
                        "host keeps empty room and announces friend's departure");
                    app.RequestQuit();
                    Expect(!app.m_running && !app.m_afterRoomLeave && !app.RoomOpen(),
                        "window close without friend closes immediately");
                }
                else
                    Expect(!app.RoomOpen() && !app.m_toasts.Items().empty(), "guest sees room closure reason");
                seats[0] = figure;
                WriteSeats(*game, SessionFile(*game), seats);
            }
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Context context;
                Room room(app);
                room.Connect(true);
                const std::string original = app.m_profiles.active;
                const std::string target = profiles::Add(app.m_profiles, "", 1).id;
                app.SwitchProfile(target);
                Expect(bool(app.m_afterRoomLeave) && app.m_profiles.active == original && app.RoomOpen(),
                    "switching profile asks before changing data folders");
                app.ResolveRoomLeave(false);
                Expect(app.m_profiles.active == original && app.RoomOpen(), "Stay cancels profile switch");
                app.SwitchProfile(target);
                app.ResolveRoomLeave(true);
                Expect(app.m_profiles.active == target && !app.RoomOpen(), "Leave then switches profile");
                room.Connect(true);
                app.OpenRoomPicker();
                Expect(app.m_roomFigures.figures.empty() && !app.m_roomFigures.Find(figure),
                    "picker reads only the active profile's collection");
                Expect(!app.SetRoomSeat(0, figure), "picker refuses another profile's figure");
                app.GoTo(Page::Collection);
                app.m_s->collection->OpenAdd();
                Expect(app.RoomOpen() && app.m_s->collection->TakesBack(),
                    "empty picker Add opens Collection's add flow with the room open");
                const fs::path own = datadir::FromText(app.m_roomFigures.sourceFolder) / "own.sky";
                fs::create_directories(own.parent_path());
                fs::copy_file(datadir::FromText(figure), own);
                app.OpenRoomPicker();
                Expect(app.SetRoomSeat(0, datadir::Text(own)), "picker seats the switched profile's own figure");
                Expect(app.m_roomFigures.figures.size() == 1, "picker excludes the first profile's other figures");
                app.CloseOnline();
                app.SwitchProfile(original);
            }
            {
                LauncherConfig config;
                App app(*FindGame("swapforce"), LauncherConfigPath(), config);
                Context context;
                app.m_online.game = "swapforce";
                app.m_online.preview = online::Status{};
                app.m_online.preview->state = online::State::InRoom;
                app.m_online.preview->host = false;
                const auto halves = collection::CreateSwapper(2000, 1000);
                const auto alternate = data / "alternate-collection";
                fs::create_directories(alternate);
                const auto top = alternate / datadir::FromText(halves.top).filename();
                const auto bottom = alternate / datadir::FromText(halves.bottom).filename();
                fs::copy_file(datadir::FromText(halves.top), top);
                fs::copy_file(datadir::FromText(halves.bottom), bottom);
                fs::copy_file(
                    datadir::FromText(collection::Folder()) / "collection.json", alternate / "collection.json");
                app.OpenRoomPicker();
                app.m_roomFigures.sourceFolder = datadir::Text(alternate);
                app.m_roomFigures.Load();
                // The collection's own path format (collection::PathOf): the folder, "/", the file, on every system.
                const std::string topPath = datadir::Text(alternate) + "/" + datadir::Text(top.filename());
                const std::string bottomPath = datadir::Text(alternate) + "/" + datadir::Text(bottom.filename());
                Expect(app.SetRoomSeat(0, topPath), "room picker seats a paired swapper");
                const auto* swap = collection::Catalogue::Get().FindGame("swapforce");
                auto inputs = online::ReadRoomInputs(config, app.m_profiles.Active(), *swap, std::string(64, 'a'),
                    false, SessionFile(*swap), data, false);
                Expect(inputs.player.figures.size() == 2 && inputs.localFigureFiles.size() == 2 &&
                           inputs.localFigureFiles[0] == topPath && inputs.localFigureFiles[1] == bottomPath,
                    "room picker and inputs preserve both halves from the selected collection");
                Expect(!app.SetRoomSeat(1, halves.top), "guest cannot change the other seat");
                WriteSeats(*game, SessionFile(*game), seats);
            }
            {
                online::Status status;
                status.state = online::State::Waiting;
                Expect(RoomStatusText(status) == "Waiting for a friend", "sidebar waiting");
                for (auto state : { online::State::Connecting, online::State::Greeting })
                {
                    status.state = state;
                    Expect(RoomStatusText(status) == "Connecting...", "sidebar connecting and greeting");
                }
                status.state = online::State::InRoom;
                status.haveRemote = true;
                status.remote.pseudo = "Kaos";
                Expect(RoomStatusText(status) == "Kaos is here, not ready yet", "sidebar friend present");
                status.remoteReady = true;
                Expect(RoomStatusText(status) == "Kaos is ready", "sidebar friend ready");
                status.localReady = true;
                Expect(RoomStatusText(status) == "Both players are ready", "sidebar both ready");
                status.state = online::State::Started;
                Expect(RoomStatusText(status) == "Starting the session...", "sidebar starting");
            }
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Context context;
                Room room(app);
                room.Connect(true);
                app.m_online.lobby.SetReady(true);
                room.peer.SetReady(true);
                Expect(room.Until([&] { return app.m_online.lobby.Get().CanStart(); }), "start fixture ready");
                app.m_page = Page::Settings;
                Expect(app.m_online.lobby.StartGame(), "start fixture queues Start");
                const auto before = online::Encode(app.m_online.inputs.player);
                const auto files = app.m_online.inputs.localFigureFiles;
                seats[0].clear();
                WriteSeats(*game, SessionFile(*game), seats);
                app.m_online.refreshAt = 0;
                app.PollOnline();
                Expect(online::Encode(app.m_online.inputs.player) == before &&
                           app.m_online.inputs.localFigureFiles == files,
                    "bug 9: figure refresh during Start keeps the agreed session inputs");
                Expect(room.Until([&] { return room.peer.Get().state == online::State::Started; }),
                    "start fixture completes");
                app.PollOnline();
                const auto error = app.m_online.error;
                Expect(std::any_of(app.m_toasts.Items().begin(), app.m_toasts.Items().end(),
                           [](const auto& toast) { return toast.text == "The host started the session"; }),
                    "starting from another page announces the session before attempting launch");
                app.PollOnline();
                Expect(app.m_online.lobby.Get().state != online::State::Started && !error.empty() &&
                           app.m_online.error == error,
                    "bug 3: failed launch closes Started once and preserves its help card");
                app.m_online.port[0] = '\0';
                app.m_online.mode = online::Mode::Host;
                app.BeginRoom();
                app.PollOnline();
                Expect(app.m_online.error != error, "bug 3: invalid new room cannot launch the previous session");
            }
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Context context;
                Room room(app);
                room.Connect(false);
                app.m_page = Page::Collection;
                room.peer.Leave();
                Expect(room.Until([&] { return app.m_online.lobby.Get().state == online::State::Ended; }, true),
                    "room end fixture");
                app.PollOnline();
                Expect(app.m_toasts.Items().size() == 1 &&
                           app.m_toasts.Items().front().text == "Your friend left the room",
                    "bug 7: room end off the Online page toasts the reason once");
                app.m_online.rejoinUntil = SDL_GetTicks() + 120000;
                app.m_online.rejoinAt = 100;
                app.CloseOnline();
                Expect(app.m_online.rejoinUntil == 0 && app.m_online.rejoinAt == 0,
                    "bug 10: closing a room cancels rejoin deadlines");
            }
            for (int scenario : { 0, 1, 2 })
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Context context;
                Room room(app);
                room.Connect(true);
                ready(app);
                if (scenario == 0)
                {
                    SDL_Event event{};
                    event.type = SDL_EVENT_DROP_FILE;
                    const std::string path = datadir::Text(disc);
                    event.drop.data = path.c_str();
                    app.HandleEvent(event);
                }
                else
                {
                    app.m_page = Page::Collection;
                    app.m_s->fromCopy = scenario == 2;
                    app.m_s->discPath = datadir::Text(data / "missing.iso");
                    app.DiscStillThere();
                }
                Expect(!app.RoomOpen() && app.m_page == Page::Play && !app.m_toasts.Items().empty(),
                    "bug 8: dropped disc or missing disc/copy closes the room with a toast");
            }
            {
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Context context;
                Room room(app);
                ready(app);
                app.m_online.mode = online::Mode::Host;
                app.LoadOnlineFields();
                Write(FoldersOf("ssa").saves / "online-replay.txt", "stale replay");
                app.BeginRoom();
                Expect(!fs::exists(FoldersOf("ssa").saves / "online-replay.txt") &&
                           std::none_of(app.m_online.inputs.save.begin(), app.m_online.inputs.save.end(),
                               [](const auto& file) { return file.path == "online-replay.txt"; }),
                    "bug 4: opening a room removes a replay marker left in real saves");
                app.CloseOnline();
            }
            {
                Write(data / "online" / "destination.txt", "unused\nssa\n\n");
                savebackup::SetGameRunning(true, data / "online" / "session" / "saves");
                LauncherConfig config;
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                app.m_session.Poll(SDL_GetTicks());
                CollectionModel model;
                model.Load();
                Expect(model.RunningGame() == "ssa" && !model.CanEditSeats() && !model.SetSeat(0, figure),
                    "bug 5: the session marker makes the player's portal read-only");
                std::string error;
                Expect(!model.Delete(figure, &error) && fs::exists(datadir::FromText(figure)),
                    "bug 5: Collection cannot delete figures during a session");
                collection::Figure current;
                Expect(collection::Read(figure, current), "read-only figure fixture");
                auto changed = current.stats;
                changed.gold = 123;
                const auto before = savebackup::ReadFile(datadir::FromText(figure));
                Expect(!SaveEditedStats(figure, current.stats, changed, "", &error) &&
                           savebackup::ReadFile(datadir::FromText(figure)) == before,
                    "bug 5: a figure editor opened before the session cannot write back");
                app.m_toybox = "ssa";
                app.StartToyboxLink();
                Expect(!app.m_link, "bug 5: a separate Toy Box cannot link to the player's portal during a session");
                app.m_toybox.clear();
                app.OpenToyboxWindow();
                Expect(app.m_toyboxProcess == nullptr && !app.m_toasts.Items().empty() &&
                           app.m_toasts.Items().back().text ==
                               "During the session, change figures in the game (F3 > Toy Box).",
                    "bug 5: the launcher's Toy Box stays closed during a session");
                if (app.m_toyboxProcess)
                {
                    SDL_KillProcess(app.m_toyboxProcess, true);
                    SDL_WaitProcess(app.m_toyboxProcess, true, nullptr);
                    SDL_DestroyProcess(app.m_toyboxProcess);
                    app.m_toyboxProcess = nullptr;
                }
                savebackup::SetGameRunning(false, data / "online" / "session" / "saves");
                fs::remove(data / "online" / "destination.txt");
            }
        }

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
                     "SKY_SAVE_DIR", "SKY_LAUNCHER_LANGUAGE", "SKY_COLLECTION" })
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
                    : std::string_view(name) == "SKY_COLLECTION" ? datadir::FromText(collection::Folder())
                    : std::string_view(name) == "SKY_SAVE_DIR"   ? saves
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
                     "runtime", "platform", "fps", "seats", "invalid", "unknown" })
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
            // "Play together again" from the game: once the saves are back, the room is set up again with its code.
            prepare(true, "quit");
            Write(session / "saves" / "online-replay.txt", "replay\n");
            {
                LauncherConfig config;
                config["ssa.online"]["mode"] = "host";
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                app.m_online.lastSecret = 0x1234567890;
                app.m_online.keepSecret = false;
                app.RecoverOnline();
                Expect(!fs::exists(session / "saves" / "online-replay.txt"), "the replay request is consumed");
                // No disc in this test: opening the room stops at the disc check, which shows the game's page.
                Expect(app.m_online.mode == online::Mode::Host, "play together again reopens the host's room");
            }
            // A game that stopped without a reason (a crash): the room offers to play together again.
            prepare(false, nullptr);
            {
                LauncherConfig config;
                config["ssa.online"]["mode"] = "join";
                App app(*FindGame("ssa"), LauncherConfigPath(), config);
                Expect(app.m_page == Page::Online && !app.m_online.error.empty() &&
                           app.m_online.mode == online::Mode::Join && app.m_online.rejoinUntil != 0,
                    "a game that stopped during a session offers to play together again");
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
            RoomBugs(data);
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
        online::GameOnline set{ online::Mode::Join, 50000, false, "203.0.113.4:50000", true };
        fresh["giants.online"]["password"] = "obsolete";
        Expect(online::ReadGameOnline(fresh, "giants").address.empty(), "old passwords are ignored");
        online::WriteGameOnline(fresh, "giants", set);
        Expect(!fresh["giants.online"].contains("password"), "saving removes an old room password");
        const auto back = online::ReadGameOnline(fresh, "giants");
        Expect(back.mode == online::Mode::Join && back.port == 50000 && !back.automatic &&
                   back.address == set.address && back.crossPlatform &&
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
    std::atomic<bool> udpOnly = true;
    {
        launcher_ui::RouterLease lease(
            [&](const netplay::portmap::Options& options) {
                udpOnly = udpOnly && !options.alsoTcp && options.port == 47640;
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
    Expect(udpOnly, "router maps UDP only");
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
        std::vector<online::net::Endpoint> endpoints;
        const std::vector<online::LocalAddress> locals{ { "en0", "", "192.168.1.20" },
            { "vpn", "Radmin VPN", "26.1.2.3" } };
        constexpr uint64_t secret = 0x9876543210;
        uint64_t decoded = 0;
        auto holds = [&](const launcher_ui::Invite& invite, const char* ip, uint16_t to) {
            return online::DecodeRoomCode(invite.code, endpoints, decoded) &&
                   endpoints.front() == online::net::Endpoint{ ip, to } && decoded == secret;
        };
        auto invite = launcher_ui::InviteFor(opened, both, true, true, locals, 47640, secret);
        Expect(invite.reach == Reach::Asking && invite.code.empty() && invite.paths.empty(),
            "no code while asking the router: its answer would change it");
        invite = launcher_ui::InviteFor(opened, both, false, true, locals, 47640, secret);
        Expect(invite.reach == Reach::Anywhere && holds(invite, "203.0.113.4", 47640) &&
                   invite.address == "203.0.113.4:47640",
            "an opened router: the code starts with its public address");
        Expect(endpoints.size() == 3 && endpoints[1].ip == "192.168.1.20" && endpoints[2].ip == "26.1.2.3",
            "the same code holds home and virtual network paths");
        Expect(invite.paths == "Your friend can join from your home, over the internet or over Radmin VPN.",
            "the invitation names the paths in the code");
        invite = launcher_ui::InviteFor({}, both, false, true, locals, 47640, secret);
        Expect(invite.reach == Reach::Ipv6 && holds(invite, "198.51.100.7", 47640) && endpoints.size() == 4 &&
                   endpoints[1].ip == "2001:db8::7",
            "without a public mapping both STUN families precede the local paths");
        invite = launcher_ui::InviteFor(shared, v4, false, true, locals, 47640, secret);
        Expect(invite.reach == Reach::Shared && holds(invite, "198.51.100.7", 47640),
            "an address shared by the provider is told apart");
        invite = launcher_ui::InviteFor({}, v4, false, true, locals, 47640, secret);
        Expect(invite.reach == Reach::Refused && holds(invite, "198.51.100.7", 47640), "a refusing router");
        Expect(invite.paths == "Your friend can join from your home or over Radmin VPN.",
            "a public address the router did not open stays in the code without promising the internet");
        const std::vector<online::LocalAddress> twoHome{ { "en0", "", "192.168.1.20" }, { "en1", "", "192.168.1.21" } };
        invite = launcher_ui::InviteFor(opened, both, false, true, twoHome, 47640, secret);
        Expect(holds(invite, "203.0.113.4", 47640) && endpoints.size() == 3 &&
                   invite.paths == "Your friend can join from your home or over the internet.",
            "Ethernet and Wi-Fi at home are one path, not a virtual network");
        invite = launcher_ui::InviteFor({}, { { "198.51.100.7", 51000 }, {} }, false, false, locals, 47640, secret);
        Expect(invite.reach == Reach::Manual && holds(invite, "198.51.100.7", 51000) && endpoints[1].port == 47640,
            "a STUN mapped port supplies the code without promising outside access");
        invite = launcher_ui::InviteFor({}, both, false, false, locals, 47641, secret);
        Expect(
            invite.reach == Reach::Manual && holds(invite, "198.51.100.7", 47641) && endpoints[1].ip == "2001:db8::7",
            "automatic opening off retains all available addresses");
        invite = launcher_ui::InviteFor({}, none, false, true, locals, 47640, secret);
        Expect(invite.reach == Reach::Refused && holds(invite, "192.168.1.20", 47640),
            "no public address known: the local one");
        const online::net::Endpoint tunnel{ "203.0.113.8", 55000 };
        invite = launcher_ui::InviteFor(opened, both, false, true, locals, 47640, secret, tunnel);
        Expect(holds(invite, "203.0.113.4", 47640) && endpoints.size() == 4 && endpoints.back() == tunnel,
            "a public address, LAN, virtual network and tunnel share one code in order");
        invite = launcher_ui::InviteFor(opened, both, false, true, locals, 47640, secret, { "26.1.2.3", 47640 });
        Expect(holds(invite, "203.0.113.4", 47640) && endpoints.size() == 3,
            "a tunnel already in the code is not duplicated");
        auto many = locals;
        many.push_back({ "tailscale0", "", "100.64.1.2" });
        many.push_back({ "zt0", "", "10.30.0.2" });
        invite = launcher_ui::InviteFor({}, both, false, true, many, 47640, secret, tunnel);
        Expect(holds(invite, "198.51.100.7", 47640) && endpoints.size() == 6 && endpoints.back() == tunnel,
            "the six endpoint limit reserves the last place for a tunnel");
        invite = launcher_ui::InviteFor({}, both, false, true, many, 47640, secret, { "100.64.1.2", 47640 });
        Expect(holds(invite, "198.51.100.7", 47640) && endpoints.size() == 6 && endpoints.back().ip == "10.30.0.2",
            "a duplicate tunnel does not take a place from a local network");
        invite = launcher_ui::InviteFor({}, none, false, true, {}, 47640, secret, tunnel);
        Expect(holds(invite, "203.0.113.8", 55000) && endpoints.size() == 1 &&
                   invite.paths == "Your friend can join over the internet.",
            "a tunnel alone still offers one code");
        Expect(launcher_ui::MaskedCode("K7QD-M2XA") == "****-****", "a masked code keeps only its dashes");
    }
    launcher_ui::OnlineUiChecks::Recovery(scratch / "recovery");
    fprintf(stderr, "Online UI inputs, session recovery and router worker: %d failure(s)\n", failures);
    return failures;
}
