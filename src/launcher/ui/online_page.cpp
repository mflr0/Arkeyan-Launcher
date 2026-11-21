#include "launcher/ui/launcher_app.h"
#include "launcher/atomic_file.h"
#include "launcher/online/room_inputs.h"
#include "shared/collection/datadir.h"
#include "shared/collection/save_backup.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <fmt/format.h>
#include <stb_image_write.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#undef DrawText // windows.h's macro would rename ui::DrawText
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

namespace launcher_ui
{
    namespace online_page
    {
        std::filesystem::path Root() { return LauncherDataFolder() / "online"; }
        const char* InputError(online::InputError error)
        {
            switch (error)
            {
            case online::InputError::Picture: return Tr("Couldn't read your picture; choose it again in your profile.");
            case online::InputError::Figure:
                return Tr("Couldn't read a figure; choose your figures again in Collection.");
            case online::InputError::Save:
                return Tr("Couldn't read your saves; check that their folder is accessible.");
            case online::InputError::SaveTooLarge: return Tr("Your saves exceed 64 MB; remove unused saves first.");
            case online::InputError::None: return "";
            }
            return "";
        }
        std::string LocalAddress()
        {
#if defined(_WIN32)
            online::net::Startup();
            char hostname[256] = {};
            if (gethostname(hostname, sizeof hostname) == 0)
            {
                addrinfo hints{}, *addresses = nullptr;
                hints.ai_family = AF_INET;
                if (getaddrinfo(hostname, nullptr, &hints, &addresses) == 0)
                {
                    std::string result;
                    for (auto* a = addresses; a; a = a->ai_next)
                    {
                        char text[INET_ADDRSTRLEN] = {};
                        const auto* address = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
                        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof text) &&
                            std::string_view(text) != "127.0.0.1")
                        {
                            result = text;
                            break;
                        }
                    }
                    freeaddrinfo(addresses);
                    if (!result.empty())
                        return result;
                }
            }
#else
            ifaddrs* addresses = nullptr;
            if (getifaddrs(&addresses) == 0)
            {
                std::string result;
                for (auto* a = addresses; a; a = a->ifa_next)
                {
                    if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) ||
                        (a->ifa_flags & IFF_LOOPBACK))
                        continue;
                    char text[INET_ADDRSTRLEN] = {};
                    if (inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, text, sizeof text))
                    {
                        result = text;
                        break;
                    }
                }
                freeifaddrs(addresses);
                if (!result.empty())
                    return result;
            }
#endif
            return "127.0.0.1";
        }
    }

    RouterLease::~RouterLease()
    {
        {
            std::lock_guard lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_one();
        if (m_thread.joinable())
            m_thread.join();
    }
    void RouterLease::Open(uint16_t port)
    {
        {
            std::lock_guard lock(m_mutex);
            m_port = port;
            m_active = true;
            m_pending = true;
            ++m_generation;
        }
        if (!m_thread.joinable())
            m_thread = std::thread(&RouterLease::Work, this);
        m_wake.notify_one();
    }
    void RouterLease::Close()
    {
        {
            std::lock_guard lock(m_mutex);
            if (!m_active)
                return;
            m_active = false;
            m_pending = false;
            ++m_generation;
            m_mapping = {};
        }
        m_wake.notify_one();
    }
    netplay::portmap::Mapping RouterLease::Get()
    {
        std::lock_guard lock(m_mutex);
        return m_mapping;
    }
    bool RouterLease::Pending()
    {
        std::lock_guard lock(m_mutex);
        return m_pending;
    }
    void RouterLease::Work()
    {
        using namespace std::chrono;
        std::unique_lock lock(m_mutex);
        uint64_t seen = 0;
        auto renew = steady_clock::time_point::max();
        netplay::portmap::Mapping mapping;
        netplay::portmap::Options options;
        options.alsoTcp = true;
        for (;;)
        {
            m_wake.wait_until(lock, renew, [&] { return m_quit || seen != m_generation; });
            const bool changed = seen != m_generation;
            const bool active = m_active, quit = m_quit;
            seen = m_generation;
            if (mapping.ok && (changed || quit))
            {
                lock.unlock();
                m_unmap(mapping, options);
                lock.lock();
                mapping = {};
            }
            if (quit)
                break;
            if (!active)
            {
                renew = steady_clock::time_point::max();
                continue;
            }
            options.port = m_port;
            const uint64_t generation = seen;
            m_pending = true;
            lock.unlock();
            auto result = m_map(options);
            lock.lock();
            if (result.ok || !mapping.ok)
                mapping = std::move(result);
            if (generation == m_generation && !m_quit)
            {
                m_mapping = mapping;
                m_pending = false;
            }
            renew = steady_clock::now() + m_renewal;
        }
    }

    const char* OnlineErrorText(online::Error error, online::Mismatch mismatch)
    {
        using online::Error;
        using online::Mismatch;
        if (error == Error::Mismatch)
        {
            switch (mismatch)
            {
            case Mismatch::Protocol: return Tr("Your rooms aren't compatible; update both launchers.");
            case Mismatch::Launcher: return Tr("Your launcher versions differ; update both launchers.");
            case Mismatch::Game: return Tr("You chose different games; choose the same game.");
            case Mismatch::Disc: return Tr("Your disc editions differ; use the same disc edition.");
            case Mismatch::GameVersion: return Tr("Your game versions differ; update both games.");
            case Mismatch::Platform: return Tr("Your systems differ; both players must allow cross-platform sessions.");
            case Mismatch::None: return Tr("Your games don't match; check both games and launchers.");
            }
        }
        switch (error)
        {
        case Error::None: return "";
        case Error::PortInUse: return Tr("This port is busy; choose another port.");
        case Error::BadAddress: return Tr("This address isn't valid; paste the host's IP:port.");
        case Error::CannotConnect:
            return Tr("The host didn't answer; check the address and ask them to open the room.");
        case Error::Timeout: return Tr("The other player stopped answering; check your connection and reconnect.");
        case Error::WrongPassword: return Tr("The password doesn't match; ask the host and try again.");
        case Error::RoomFull: return Tr("This room is full; ask the host to open another room.");
        case Error::Left: return Tr("The other player left; open or join a room again.");
        case Error::Broken: return Tr("The connection failed; open or join a room again.");
        case Error::Mismatch: break;
        }
        return "";
    }

    std::string OnlineEndText(std::string_view reason)
    {
        using online::Error;
        using online::Mismatch;
        if (reason.empty() || reason == "quit")
            return {};
        std::string text;
        if (reason == "timeout")
            text = OnlineErrorText(Error::Timeout, Mismatch::None);
        else if (reason == "transport")
            text = OnlineErrorText(Error::Broken, Mismatch::None);
        else if (reason == "protocol")
            text = OnlineErrorText(Error::Mismatch, Mismatch::Protocol);
        else if (reason == "game")
            text = OnlineErrorText(Error::Mismatch, Mismatch::Game);
        else if (reason == "image")
            text = OnlineErrorText(Error::Mismatch, Mismatch::Disc);
        else if (reason == "runtime")
            text = OnlineErrorText(Error::Mismatch, Mismatch::GameVersion);
        else if (reason == "platform")
            text = OnlineErrorText(Error::Mismatch, Mismatch::Platform);
        else if (reason == "desync")
            text = Tr("The games lost sync; reopen the room and try again");
        else if (reason == "fps")
            text = Tr("Your frame rates differ; choose the same frame rate");
        else if (reason == "seats")
            text = Tr("The portal figures didn't match; choose your figures again in Collection");
        else
            text = Tr("The online session ended unexpectedly; reopen the room and try again");
        if (text.ends_with('.'))
            text.pop_back();
        return text;
    }

    void App::OpenOnline()
    {
        if (!m_session.Running().empty() || m_s->installed.runtime.empty())
            return;
        RecoverOnline();
        if (m_online.recovering)
        {
            Toast(m_online.error.empty()
                      ? Tr("Couldn't restore the session's saves or figures; check the data folder and restart.")
                      : m_online.error,
                ui::ToastKind::Error);
            return;
        }
        if (!m_online.game.empty() && m_online.game != m_s->game.id)
            CloseOnline();
        m_online.game = m_s->game.id;
        GoTo(Page::Online);
    }

    void App::CloseOnline()
    {
        m_online.lobby.Leave();
        m_online.router.Close();
        m_online.game.clear();
        m_online.with.clear();
        if (!m_online.recovering)
            m_online.error.clear();
        m_online.launched = false;
        m_online.preview.reset();
        m_online.password[0] = '\0';
    }

    void App::RecoverOnline()
    {
        std::ifstream recovery(online_page::Root() / "destination.txt");
        std::string path;
        if (!std::getline(recovery, path) || path.empty())
            return;
        std::string game, peer;
        std::getline(recovery, game);
        std::getline(recovery, peer);
        recovery.close();
        m_online.realSaves = datadir::FromText(path);
        if (savebackup::GameRunning(online_page::Root() / "session" / "saves"))
        {
            m_online.recovering = true;
            m_online.game = game;
            m_online.with = peer;
            return;
        }
        const auto endedFile = online_page::Root() / "session" / "saves" / "online-ended.txt";
        std::ifstream ended(endedFile);
        std::string line, reason;
        while (std::getline(ended, line))
            if (line.starts_with("reason="))
            {
                reason = line.substr(7);
                if (reason.ends_with('\r'))
                    reason.pop_back();
                break;
            }
        ended.close();
        std::string error;
        if (!online::FinishSession(online_page::Root() / "session", m_online.realSaves, error))
        {
            m_online.recovering = true;
            m_online.error = Tr("Couldn't restore the session's saves or figures; check the data folder and restart.");
            return;
        }
        std::error_code ec;
        std::filesystem::remove(online_page::Root() / "destination.txt", ec);
        std::filesystem::remove(endedFile, ec);
        if (const std::string text = OnlineEndText(reason); !text.empty())
            Toast(text, ui::ToastKind::Error);
        m_online.recovering = false;
        m_online.error.clear();
        if (!m_online.launched && m_session.Running().empty())
        {
            m_online.with.clear();
            m_online.game.clear();
        }
    }

    void App::PollOnline()
    {
        if (m_online.preview)
            return;
        if (m_online.recovering && SDL_GetTicks() >= m_online.refreshAt)
        {
            m_online.refreshAt = SDL_GetTicks() + 1000;
            RecoverOnline();
        }
        m_online.lobby.Poll();
        const auto& status = m_online.lobby.Get();
        if (status.state != m_online.lastState)
        {
            m_online.lastState = status.state;
            if (m_page == Page::Online)
                RequestFocus(FocusTo::Page);
        }
        if (status.state == online::State::Ended)
            m_online.router.Close();
        if (status.state == online::State::Started && !m_online.launched)
            LaunchOnline();
        if (m_online.launched || m_online.game.empty() || SDL_GetTicks() < m_online.refreshAt)
            return;
        m_online.refreshAt = SDL_GetTicks() + 1000;
        if (status.state != online::State::Waiting && status.state != online::State::InRoom)
            return;
        const auto* game = collection::Catalogue::Get().FindGame(m_online.game);
        if (!game)
            return;
        auto inputs = online::ReadRoomInputs(m_config, m_profiles.Active(), *game, m_online.inputs.hello.xexSha256,
            status.host, SessionFile(*game), m_online.realSaves, false);
        if (inputs.error != online::InputError::None)
        {
            m_online.error = online_page::InputError(inputs.error);
            m_online.lobby.SetReady(false);
            return;
        }
        m_online.error.clear();
        if (online::Encode(inputs.player) != online::Encode(m_online.inputs.player))
        {
            m_online.inputs.player = inputs.player;
            m_online.inputs.localFigureFiles = std::move(inputs.localFigureFiles);
            m_online.lobby.UpdatePlayer(inputs.player);
        }
    }

    void App::LaunchOnline()
    {
        auto& room = m_online;
        const auto status = room.lobby.Get();
        room.launched = true;
        GameSlot* slot = nullptr;
        for (auto& each : m_slots)
            if (each->game.id == room.game)
                slot = each.get();
        const auto* game = collection::Catalogue::Get().FindGame(room.game);
        if (!slot || !game)
        {
            CloseOnline();
            return;
        }
        GameLaunch launch = PrepareGameLaunch(slot->game, m_config, GameSettingsPath(), slot->installed.runtime);
        if (GameSettingsPath() != m_configPath)
            launch.variables.emplace_back("SKY_SETTINGS", GameSettingsPath());
        online::SessionPlan plan;
        plan.host = status.host;
        plan.hostAddress = room.joinedHost;
        plan.start = status.start;
        if (!status.host)
            plan.start.port = room.joinedPort;
        plan.hostPlayer = status.host ? room.inputs.player : status.remote;
        plan.guestPlayer = status.host ? status.remote : room.inputs.player;
        plan.settings = status.host ? room.inputs.settings : status.settings;
        plan.save = status.host ? room.inputs.save : status.save;
        plan.localFigureFiles = room.inputs.localFigureFiles;
        std::string error;
        if (launch.runtime.empty() ||
            !launcher_files::WriteAtomic(online_page::Root() / "destination.txt",
                datadir::Text(room.realSaves) + "\n" + room.game + "\n" + status.remote.pseudo + "\n") ||
            !online::PrepareSession(*game, plan, online_page::Root() / "session", launch, error) ||
            !m_session.Start(room.game, launch, slot->discPath, error))
        {
            RecoverOnline();
            CloseOnline();
            room.error = Tr("Couldn't start the online game; check your game installation and try again.");
            Toast(room.error, ui::ToastKind::Error);
            return;
        }
        room.with = status.remote.pseudo;
        m_sessionIso = slot->discPath;
        m_sessionStart = SDL_GetTicks();
        RememberProfileGame(room.game);
        StopDiscChecks();
        m_music.Update({}, !GameMusicOn(), GameMusicVolume(m_config));
        m_s = slot;
        GoTo(Page::Play);
    }

    void App::DrawOnline()
    {
        using namespace online;
        auto& room = m_online;
        const Status status = room.preview ? *room.preview : room.lobby.Get();
        BeginSettingsPage("##onlinecolumn", Tr("Online"), true, true);
        BeginCard("##online", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        const bool idle = status.state == State::Idle || status.state == State::Ended;
        if (idle)
        {
            const char* tabs[] = { Tr("Host"), Tr("Join") };
            ui::Tabs("##onlinetabs", tabs, &room.tab);
            TakeFocus(FocusTo::Page);
            if (ui::BeginSettings("##onlinefields"))
            {
                if (room.tab == 0)
                {
                    ui::SettingRow(Tr("Port"));
                    ui::TextField(
                        "##onlineport", room.port, sizeof room.port, { .flags = ImGuiInputTextFlags_CharsDecimal });
                    ui::SettingRow(Tr("Open the port automatically"));
                    ui::Toggle("##onlineautomatic", &room.automatic);
                }
                else
                {
                    ui::SettingRow(Tr("Address"));
                    ui::TextField("##onlineaddress", room.address, sizeof room.address, { .hint = "IP:port" });
                }
                ui::SettingRow(Tr("Password (optional)"));
                ui::TextField(
                    "##onlinepassword", room.password, sizeof room.password, { .flags = ImGuiInputTextFlags_Password });
                ui::EndSettings();
            }
            if (ui::Button(room.tab == 0 ? Tr("Open room") : Tr("Connect"), { .kind = ui::ButtonKind::Primary }) &&
                !room.preview)
            {
                room.error.clear();
                const auto* game = collection::Catalogue::Get().FindGame(m_s->game.id);
                uint16_t port = 0;
                std::string host;
                if (room.tab == 0 && !net::ParseAddress(std::string("127.0.0.1:") + room.port, DefaultPort, host, port))
                    room.error = Tr("This port isn't valid; choose a number from 1 to 65535.");
                else if (room.tab == 1 && !net::ParseAddress(room.address, DefaultPort, host, port))
                    room.error = OnlineErrorText(Error::BadAddress, Mismatch::None);
                else if (game && !room.recovering && DiscStillThere() && m_session.Running().empty())
                {
                    room.game = m_s->game.id;
                    room.realSaves = FoldersOf(room.game).saves;
                    room.inputs = ReadRoomInputs(m_config, m_profiles.Active(), *game, m_s->check.xexSha256,
                        room.tab == 0, SessionFile(*game), room.realSaves);
                    if (room.inputs.error != InputError::None)
                        room.error = online_page::InputError(room.inputs.error);
                    else if (room.tab == 0)
                    {
                        HostOptions options;
                        options.port = port;
                        options.password = room.password;
                        options.hello = room.inputs.hello;
                        options.player = room.inputs.player;
                        options.settings = room.inputs.settings;
                        options.save = room.inputs.save;
                        if (room.lobby.Host(std::move(options)))
                        {
                            room.localAddress = online_page::LocalAddress();
                            if (room.automatic && m_shots.empty())
                                room.router.Open(port);
                        }
                    }
                    else
                    {
                        room.joinedHost = host;
                        room.joinedPort = port;
                        room.lobby.Join({ room.address, room.password, room.inputs.hello, room.inputs.player });
                    }
                }
            }
        }
        else
        {
            ui::SectionHeader(status.host ? Tr("Your room") : Tr("Online room"), nullptr, false);
            if (status.host)
            {
                const auto mapping = room.router.Get();
                uint32_t ip = 0;
                const bool publicIp = mapping.ok && netplay::portmap::ParseIpv4(mapping.externalIp, ip) &&
                                      netplay::portmap::IsPublicIpv4(ip);
                const std::string address = fmt::format("{}:{}", publicIp ? mapping.externalIp : room.localAddress,
                    publicIp ? std::to_string(mapping.externalPort) : std::string(room.port));
                ui::Text(ui::TextStyle::Label, address.c_str());
                ImGui::SameLine(0, ui::space::S);
                if (ui::Button(Tr("Copy"), { .size = ui::Size::Small }))
                    SDL_SetClipboardText(address.c_str());
                TakeFocus(FocusTo::Page);
                const std::string note =
                    room.router.Pending() ? Tr("Opening the router's port...")
                    : publicIp            ? ""
                                          : Tf("Port not opened; open port {} UDP and TCP on your router.", room.port);
                if (!note.empty())
                    ui::Text(
                        ui::TextStyle::Caption, note.c_str(), ui::color::InkSoft, ImGui::GetContentRegionAvail().x);
            }
            const Player& p1 = status.host ? room.inputs.player : status.remote;
            const Player& p2 = status.host ? status.remote : room.inputs.player;
            const float playerWidth = (ImGui::GetContentRegionAvail().x - ui::space::M) / 2;
            for (int i = 0; i < 2; ++i)
            {
                const bool present = (i == (status.host ? 0 : 1)) || status.haveRemote;
                const Player& player = i == 0 ? p1 : p2;
                ImGui::PushID(i);
                ui::BeginCard("##player", ImVec2(playerWidth, 0),
                    { .inset = true, .padding = ui::space::M, .flags = ImGuiChildFlags_AutoResizeY });
                if (present)
                {
                    if (room.pictureBytes[i] != player.picture)
                    {
                        if (room.pictures[i])
                            SDL_DestroyTexture(room.pictures[i]);
                        room.pictures[i] = player.picture.empty() ? nullptr : LoadPng(m_renderer, player.picture);
                        room.pictureBytes[i] = player.picture;
                    }
                    const float size = ui::height::Large;
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const unsigned color = collection::ElementRgb(collection::Element(std::min<int>(player.color, 9)));
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        ImVec2(at.x + size / 2, at.y + size / 2), size / 2, ui::Col(color));
                    if (room.pictures[i])
                        ImGui::GetWindowDrawList()->AddImage(
                            Ref(room.pictures[i]), at, ImVec2(at.x + size, at.y + size));
                    else
                    {
                        const std::string initials = profiles::Initials(player.pseudo);
                        ui::DrawText(ImGui::GetWindowDrawList(), ImVec2(at.x + ui::space::S, at.y + ui::space::M),
                            ui::Col(ui::InkOn(color, ui::color::Panel)), initials.c_str());
                    }
                    ImGui::GetWindowDrawList()->AddCircle(
                        ImVec2(at.x + size / 2, at.y + size / 2), size / 2, ui::Col(color), 0, ui::stroke::Selection);
                    ImGui::Dummy(ImVec2(size, size));
                    ImGui::SameLine(0, ui::space::M);
                    ImGui::BeginGroup();
                    const std::string name = Tf("Player {} · {}", i + 1, player.pseudo);
                    ui::Text(ui::TextStyle::Label, name.c_str(), ui::color::Ink, ImGui::GetContentRegionAvail().x);
                    std::string figures;
                    for (const auto& figure : player.figures)
                    {
                        if (!figures.empty())
                            figures += " · ";
                        figures += figure.name;
                    }
                    ui::Text(ui::TextStyle::Body, figures.empty() ? Tr("No figures chosen") : figures.c_str(),
                        ui::color::InkSoft, ImGui::GetContentRegionAvail().x);
                    ImGui::EndGroup();
                    const bool local = i == (status.host ? 0 : 1);
                    bool ready = local ? status.localReady : status.remoteReady;
                    if (ui::Toggle(Tr("Ready"), &ready, !local || status.state != State::InRoom) && !room.preview)
                        room.lobby.SetReady(ready);
                    if (local && status.state == State::InRoom)
                        TakeFocus(FocusTo::Page);
                }
                else
                    ui::Text(ui::TextStyle::Body, Tr("Waiting for the other player..."));
                ui::EndCard();
                ImGui::PopID();
                if (i == 0)
                    ImGui::SameLine(0, ui::space::M);
            }
            const Settings& settings = status.host ? room.inputs.settings : status.settings;
            std::string timing = Tr("Measuring the connection...");
            if (status.rttMs >= 0)
                timing = status.inputDelay == 1
                             ? Tf("Round trip: {} ms · Input delay: 1 frame", int(std::lround(status.rttMs)))
                             : Tf("Round trip: {} ms · Input delay: {} frames", int(std::lround(status.rttMs)),
                                   status.inputDelay);
            ui::Text(ui::TextStyle::Body, timing.c_str(), ui::color::InkSoft);
            if (status.host || status.haveSettings)
            {
                auto language = [](const std::string& code) {
                    for (const auto& choice : AllLanguageChoices)
                        if (code == choice.value)
                            return std::string(Tr(choice.label));
                    return code.empty() ? std::string(Tr("Game default")) : code;
                };
                const std::string line =
                    Tf("{} FPS · Text: {} · Voices: {}", settings.fpsLimit, language(settings.language),
                        settings.voices.empty() ? std::string(Tr("Same as text")) : language(settings.voices));
                ui::Text(ui::TextStyle::Body, line.c_str(), ui::color::InkSoft, ImGui::GetContentRegionAvail().x);
                if (settings.nightmareFromStart)
                    ui::Text(ui::TextStyle::Caption, Tr("Nightmare from the start"));
            }
            if (status.host)
            {
                if (ui::Button(Tr("Start"),
                        { .kind = ui::ButtonKind::Primary, .disabled = !status.CanStart() || !room.error.empty() }) &&
                    !room.preview)
                    room.lobby.StartGame();
                ImGui::SameLine(0, ui::space::S);
            }
            if (ui::Button(Tr("Leave")))
            {
                CloseOnline();
                GoTo(Page::Play);
            }
            TakeFocus(FocusTo::Page);
            if (status.lastRefusal.error != Error::None)
                ui::Text(ui::TextStyle::Caption, OnlineErrorText(status.lastRefusal.error, status.lastRefusal.mismatch),
                    ui::color::Warning, ImGui::GetContentRegionAvail().x);
        }
        const std::string error = room.error.empty() ? OnlineErrorText(status.error, status.mismatch) : room.error;
        if (!error.empty())
            ui::Text(ui::TextStyle::Body, error.c_str(), ui::color::Warning, ImGui::GetContentRegionAvail().x);
        EndCard();
        EndSettingsPage();
    }
}

namespace launcher_ui
{
    void App::TestOnlineScript(uint64_t now)
    {
        using namespace online;
        constexpr int ErrorCount = int(Error::Broken);
        constexpr int MismatchCount = int(Mismatch::Platform);
        constexpr const char* endReasons[] = { "quit", "timeout", "desync", "transport", "protocol", "game", "image",
            "runtime", "platform", "fps", "seats", "invalid" };
        if (m_testStep >= 4 + ErrorCount + MismatchCount + 5 + 6 + int(std::size(endReasons)))
        {
            m_running = false;
            return;
        }
        m_page = Page::Online;
        auto& room = m_online;
        room.preview = Status{};
        m_toasts.Clear();
        auto& status = *room.preview;
        room.error.clear();
        room.localAddress = "192.0.2.10";
        room.inputs.player = { "Spyro", "E000000000000001", 4, {},
            { { 16, 0, 0, "Spyro", std::vector<uint8_t>(FigureDumpBytes, 1) } } };
        room.inputs.settings = { 60, "en", "fr", false, 0, 0 };
        if (m_testStep < 2)
        {
            room.tab = m_testStep;
            m_testShot = m_testStep == 0 ? "online-00-host" : "online-01-join";
        }
        else if (m_testStep < 4)
        {
            status.host = true;
            status.state = m_testStep == 2 ? State::Waiting : State::InRoom;
            status.haveRemote = m_testStep == 3;
            status.remote = { "Kaos", "E000000000000002", 7, {},
                { { 7, 0, 0, "Gill Grunt", std::vector<uint8_t>(FigureDumpBytes, 2) } } };
            status.localReady = status.remoteReady = status.haveRemote;
            status.rttMs = 42;
            status.inputDelay = 3;
            m_testShot = m_testStep == 2 ? "online-02-waiting" : "online-03-ready";
        }
        else if (m_testStep < 4 + ErrorCount)
        {
            status.state = State::Ended;
            status.error = Error(m_testStep - 3);
            m_testShot = fmt::format("online-error-{:02}", m_testStep - 3);
        }
        else if (m_testStep < 4 + ErrorCount + MismatchCount)
        {
            status.state = State::Ended;
            status.error = Error::Mismatch;
            status.mismatch = Mismatch(m_testStep - 3 - ErrorCount);
            m_testShot = fmt::format("online-mismatch-{:02}", int(status.mismatch));
        }
        else if (m_testStep < 4 + ErrorCount + MismatchCount + 5)
        {
            const int inputError = m_testStep - 4 - ErrorCount - MismatchCount + 1;
            if (inputError <= 4)
                room.error = online_page::InputError(InputError(inputError));
            else
                room.error = Tr("Couldn't start the online game; check your game installation and try again.");
            m_testShot = fmt::format("online-input-{:02}", inputError);
        }
        else
        {
            const int extra = m_testStep - 4 - ErrorCount - MismatchCount - 5;
            if (extra < 2)
            {
                status.host = extra == 1;
                status.state = State::InRoom;
                status.haveRemote = true;
                status.remote = { "Kaos", "E000000000000002", 7, {}, {} };
                status.haveSettings = true;
                status.settings = room.inputs.settings;
                status.rttMs = 42;
                status.inputDelay = extra == 0 ? 1 : 2;
                if (extra == 1)
                    status.lastRefusal.error = Error::WrongPassword;
                m_testShot = extra == 0 ? "online-guest" : "online-refusal";
            }
            else if (extra == 2)
            {
                m_page = Page::Settings;
                m_tab = SettingsTab::Launcher;
                m_testShot = "online-settings";
            }
            else if (extra == 3)
            {
                m_page = Page::Play;
                m_s->disc = DiscState::Ready;
                m_s->installed.runtime = "display-only";
                m_s->skipUpdate = true;
                m_testShot = "online-game-button";
            }
            else if (extra < 6)
            {
                room.tab = 0;
                room.error =
                    extra == 4
                        ? Tr("This port isn't valid; choose a number from 1 to 65535.")
                        : Tr("Couldn't restore the session's saves or figures; check the data folder and restart.");
                m_testShot = extra == 4 ? "online-invalid-port" : "online-recovery-error";
            }
            else
            {
                m_page = Page::Play;
                m_s->disc = DiscState::Ready;
                m_s->installed.runtime = "display-only";
                m_s->skipUpdate = true;
                const char* reason = endReasons[extra - 6];
                if (const std::string text = OnlineEndText(reason); !text.empty())
                    Toast(text, ui::ToastKind::Error);
                m_testShot = fmt::format("online-ended-{}", reason);
            }
        }
        if (status.haveRemote)
        {
            const uint8_t pixels[] = { 240, 240, 240, 255, 80, 80, 90, 255, 80, 80, 90, 255, 240, 240, 240, 255 };
            stbi_write_png_to_func(
                [](void* context, void* data, int size) {
                    auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
                    auto* begin = static_cast<uint8_t*>(data);
                    bytes.insert(bytes.end(), begin, begin + size);
                },
                &status.remote.picture, 2, 2, 4, pixels, 8);
        }
        m_testAt = now + 150;
    }
}
