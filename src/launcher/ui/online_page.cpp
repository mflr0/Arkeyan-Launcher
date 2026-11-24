#include "launcher/ui/launcher_app.h"
#include "launcher/atomic_file.h"
#include "launcher/online/room_code.h"
#include "launcher/online/room_inputs.h"
#include "shared/collection/datadir.h"
#include "shared/collection/save_backup.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
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
    void RouterLease::Open(uint16_t port, bool map)
    {
        {
            std::lock_guard lock(m_mutex);
            m_port = port;
            m_mapPort = map;
            m_active = true;
            m_pending = true;
            m_seen = {};
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
            m_seen = {};
        }
        m_wake.notify_one();
    }
    netplay::portmap::Mapping RouterLease::Get()
    {
        std::lock_guard lock(m_mutex);
        return m_mapping;
    }
    online::stun::Result RouterLease::Seen()
    {
        std::lock_guard lock(m_mutex);
        return m_seen;
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
        options.alsoTcp = false;
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
            const bool mapPort = m_mapPort;
            const uint64_t generation = seen;
            m_pending = true;
            lock.unlock();
            netplay::portmap::Mapping result;
            if (mapPort)
            {
                result = m_map(options);
                if (result.ok)
                    SDL_Log("[online] router: port %u opened with %s (outside port %u)", unsigned(options.port),
                        result.method.c_str(), unsigned(result.externalPort));
                else
                    SDL_Log("[online] router: port %u not opened: %s", unsigned(options.port), result.error.c_str());
            }
            // The public addresses once per room, not at each renewal; not at all for a room already closed.
            std::optional<online::stun::Result> found;
            if (changed && m_lookup)
            {
                bool current = false;
                {
                    std::lock_guard check(m_mutex);
                    current = generation == m_generation && !m_quit;
                }
                if (current)
                {
                    found = m_lookup();
                    SDL_Log("[online] public address (STUN): IPv4 %s, IPv6 %s",
                        found->v4.ip.empty() ? "not found" : "found", found->v6.ip.empty() ? "not found" : "found");
                }
            }
            lock.lock();
            if (result.ok || !mapping.ok)
                mapping = std::move(result);
            if (generation == m_generation && !m_quit)
            {
                m_mapping = mapping;
                if (found)
                    m_seen = *found;
                m_pending = false;
            }
            renew = mapPort ? steady_clock::now() + m_renewal : steady_clock::time_point::max();
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
        case Error::CodeRequired:
            return Tr("An address alone can't open a room any more: ask your friend for the room code.");
        case Error::TooManyCodes: return Tr("Too many wrong room codes. Wait 30 seconds, then try again.");
        case Error::BadAddress: return Tr("This room code isn't valid; ask your friend to copy it again.");
        case Error::CannotConnect:
            return Tr("The host didn't answer; check the room code and ask them to open the room.");
        case Error::Timeout: return Tr("The other player stopped answering; check your connection and reconnect.");
        case Error::WrongCode: return Tr("This room code is wrong or old: ask your friend to copy it again.");
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

    std::string MaskedAddress(std::string_view address)
    {
        const size_t colon = address.rfind(':');
        std::string masked(8, '*');
        if (colon != std::string_view::npos)
            masked += address.substr(colon);
        return masked;
    }

    std::string MaskedCode(std::string_view code)
    {
        std::string masked(code);
        for (char& c : masked)
            if (c != '-')
                c = '*';
        return masked;
    }

    Invite InviteFor(const netplay::portmap::Mapping& mapping, const online::stun::Result& seen, bool pending,
        bool automatic, const std::string& localAddress, uint16_t port, uint64_t secret)
    {
        using Reach = Invite::Reach;
        Invite invite;
        invite.homeCode = online::EncodeRoomCode(localAddress, port, secret);
        if (pending)
            return invite;
        const auto use = [&](const std::string& host, uint16_t to) {
            invite.address =
                host.find(':') != std::string::npos ? fmt::format("[{}]:{}", host, to) : fmt::format("{}:{}", host, to);
            invite.code = online::EncodeRoomCode(host, to, secret);
        };
        uint32_t ip = 0;
        if (mapping.ok && netplay::portmap::ParseIpv4(mapping.externalIp, ip) && netplay::portmap::IsPublicIpv4(ip))
        {
            invite.reach = Reach::Anywhere;
            use(mapping.externalIp, mapping.externalPort);
            return invite;
        }
        // No public IPv4 address opened: IPv6 needs no router opening on most home connections.
        if (automatic && !seen.v6.ip.empty())
        {
            invite.reach = Reach::Ipv6;
            use(seen.v6.ip, seen.v6.port ? seen.v6.port : port);
            return invite;
        }
        invite.reach = !automatic ? Reach::Manual : mapping.ok ? Reach::Shared : Reach::Refused;
        const bool publicV4 = netplay::portmap::ParseIpv4(seen.v4.ip, ip) && netplay::portmap::IsPublicIpv4(ip);
        use(publicV4 ? seen.v4.ip : localAddress, publicV4 && seen.v4.port ? seen.v4.port : port);
        return invite;
    }

    namespace online_page
    {
        std::string SystemName(std::string_view platform)
        {
            if (platform.starts_with("macos"))
                return "macOS";
            if (platform.starts_with("windows"))
                return "Windows";
            if (platform.starts_with("linux"))
                return "Linux";
            return std::string(platform);
        }
        std::string GameTitle(const std::string& id)
        {
            const char* name = GameName(id, GameNameForm::Full);
            return name != nullptr ? name : id;
        }
    }

    OnlineHelp OnlineHelpFor(
        online::Error error, online::Mismatch mismatch, const online::Hello& mine, const online::Hello& theirs)
    {
        using online::Error;
        using online::Mismatch;
        using Action = OnlineHelp::Action;
        OnlineHelp help;
        auto both = [&](const std::string& yours, const std::string& friends) {
            help.lines.push_back(Tf("You: {}", yours));
            help.lines.push_back(Tf("Your friend: {}", friends.empty() ? std::string("?") : friends));
        };
        if (error == Error::Mismatch)
        {
            switch (mismatch)
            {
            case Mismatch::Protocol:
            case Mismatch::Launcher:
                help.title = Tr("Your launchers aren't the same version");
                both(mine.launcherVersion, theirs.launcherVersion);
                help.lines.push_back(Tr("Both update the launcher in Settings, then try again."));
                help.action = Action::Launcher;
                break;
            case Mismatch::Game:
                help.title = Tr("You chose different games");
                both(online_page::GameTitle(mine.gameId), online_page::GameTitle(theirs.gameId));
                help.lines.push_back(Tr("Both choose the same game, then try again."));
                break;
            case Mismatch::Disc:
                help.title = Tr("Your discs aren't the same edition");
                help.lines.push_back(
                    Tr("Online play needs the same edition of the disc on both computers, for example both USA."));
                break;
            case Mismatch::GameVersion:
                help.title = Tr("Your game versions differ");
                both(mine.gameVersion, theirs.gameVersion);
                help.lines.push_back(
                    Tr("The one with the older version updates the game on its page, then you try again."));
                help.action = Action::Update;
                break;
            case Mismatch::Platform:
                help.title = Tr("You play on different systems");
                both(online_page::SystemName(mine.platform), online_page::SystemName(theirs.platform));
                help.lines.push_back(Tr("Playing between systems isn't tested yet. To try it, both turn on "
                                        "Cross-platform sessions in the game's Online tab."));
                help.action = Action::Settings;
                break;
            case Mismatch::None:
                help.title = Tr("Your games don't match");
                help.lines.push_back(Tr("Check that you both have the same game, disc and launcher."));
                break;
            }
            return help;
        }
        switch (error)
        {
        case Error::None: break;
        case Error::PortInUse:
            help.title = Tr("This port is already in use");
            help.lines.push_back(Tr("Another program uses it. Choose another port in the Online tab, for example "
                                    "47641, and send your friend the new room code."));
            help.action = Action::Settings;
            break;
        case Error::CodeRequired:
            help.title = Tr("A room code is needed");
            help.lines.push_back(Tr("An address alone can't open a room any more: ask your friend for the room code."));
            help.action = Action::Settings;
            break;
        case Error::TooManyCodes:
            help.title = Tr("Guests paused for 30 seconds");
            help.lines.push_back(Tr("Too many wrong room codes. Wait 30 seconds, then try again."));
            help.action = Action::Retry;
            break;
        case Error::BadAddress:
            help.title = Tr("This room code isn't right");
            help.lines.push_back(
                Tr("Copy the whole room code from your friend's room: letters and digits in groups of four."));
            help.action = Action::Settings;
            break;
        case Error::CannotConnect:
            help.title = Tr("Your friend's room didn't answer");
            help.lines.push_back(Tr("Check that your friend's room is open: they pressed HOST A ROOM."));
            help.lines.push_back(Tr("Check the room code: ask them to copy it again."));
            help.lines.push_back(Tr("Not in the same home? Their router may block it: their room shows them how to "
                                    "fix it."));
            help.action = Action::Retry;
            break;
        case Error::Timeout:
            help.title = Tr("The connection was lost");
            help.lines.push_back(Tr("Your friend stopped answering. Check both internet connections, then try "
                                    "again."));
            help.action = Action::Retry;
            break;
        case Error::WrongCode:
            help.title = Tr("Wrong room code");
            help.lines.push_back(Tr("This room code is wrong or old: ask your friend to copy it again."));
            help.action = Action::Settings;
            break;
        case Error::RoomFull:
            help.title = Tr("This room is full");
            help.lines.push_back(Tr("Someone else is already in it: a room is for two players."));
            break;
        case Error::Left:
            help.title = Tr("Your friend left the room");
            help.lines.push_back(Tr("Open or join a room again to play."));
            help.action = Action::Retry;
            break;
        case Error::Broken:
            help.title = Tr("The connection failed");
            help.lines.push_back(Tr("Try again. If it keeps failing, both restart the launcher."));
            help.action = Action::Retry;
            break;
        case Error::Mismatch: break;
        }
        return help;
    }

    bool App::OpenOnline()
    {
        if (!m_session.Running().empty() || m_s->installed.runtime.empty())
            return false;
        RecoverOnline();
        if (m_online.recovering)
        {
            Toast(m_online.error.empty()
                      ? Tr("Couldn't restore the session's saves or figures; check the data folder and restart.")
                      : m_online.error,
                ui::ToastKind::Error);
            return false;
        }
        CloseOnline();
        m_online.game = m_s->game.id;
        GoTo(Page::Online);
        return true;
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
        // "Play together again" from the game's banner, or a game that stopped without saying why (a crash): the room
        // is set up again with the same code once the saves are back.
        const auto replayFile = online_page::Root() / "session" / "saves" / "online-replay.txt";
        std::error_code exists;
        const bool replay = std::filesystem::exists(replayFile, exists);
        const bool stopped = reason.empty() && !replay && !std::filesystem::exists(endedFile, exists);
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
        std::filesystem::remove(replayFile, ec);
        if (const std::string text = OnlineEndText(reason); !text.empty())
            Toast(text, ui::ToastKind::Error);
        m_online.recovering = false;
        m_online.error.clear();
        if ((replay || stopped) && !game.empty())
        {
            SDL_Log("[online] %s: setting the room up again with the same code",
                replay ? "play together again" : "the game stopped during the session");
            PlayTogetherAgain(game, replay);
            return;
        }
        if (!m_online.launched && m_session.Running().empty())
        {
            m_online.with.clear();
            m_online.game.clear();
        }
    }

    // The same room again after a session that stopped: the host reopens it with the last code at once; the guest
    // joins with the code it has, again every 3 seconds for 2 minutes, while the host's launcher sets it up. A crash
    // (`automatic` false) waits for the player's "Play together again" on the room's help card.
    void App::PlayTogetherAgain(const std::string& game, bool automatic)
    {
        auto& room = m_online;
        room.game.clear();
        if (const auto* info = FindGame(game))
            SelectGame(*info);
        const auto settings = online::ReadGameOnline(m_config, game);
        room.mode = settings.mode;
        room.keepSecret = room.mode == online::Mode::Host && room.lastSecret != 0;
        room.rejoinUntil = room.mode == online::Mode::Join ? SDL_GetTicks() + 120000 : 0;
        // At startup (a launcher reopened after the session) the window and its focus are not there yet.
        if (ImGui::GetCurrentContext())
            GoTo(Page::Online);
        else
            m_page = Page::Online;
        if (automatic)
            BeginRoom();
        else
            room.error = Tr("Your game stopped during the session. Play together again from where you saved.");
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
        if (m_online.rejoinUntil != 0)
        {
            const uint64_t now = SDL_GetTicks();
            if (status.state == online::State::InRoom || now >= m_online.rejoinUntil)
                m_online.rejoinUntil = 0;
            else if (status.state == online::State::Ended && status.error == online::Error::CannotConnect &&
                     now >= m_online.rejoinAt)
            {
                m_online.rejoinAt = now + 3000;
                SDL_Log("[online] the host's room does not answer yet: trying again");
                BeginRoom();
            }
        }
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
        plan.hostAddress = status.endpoint.ip;
        plan.localPort = status.localPort;
        plan.peerEndpoint = online::net::Address(status.endpoint);
        plan.start = status.start;
        if (!status.host)
            plan.start.port = status.endpoint.port;
        plan.hostPlayer = status.host ? room.inputs.player : status.remote;
        plan.guestPlayer = status.host ? status.remote : room.inputs.player;
        plan.settings = status.host ? room.inputs.settings : status.settings;
        plan.save = status.host ? room.inputs.save : status.save;
        plan.localFigureFiles = room.inputs.localFigureFiles;
        std::string error;
        // A PC whose GPU holds this game under 28 FPS starts the session one scale lower; its settings stay as they are.
        const online::PerfHint pace{ room.inputs.player.fps, room.inputs.player.gpuBound };
        const int scale = online::SessionScale(
            pace, ResolutionScaleOf(SettingGet(m_config, GameVideoSection(room.game), "resolution_scale")));
        if (scale > 0)
        {
            launch.variables.emplace_back("SKY_RESOLUTION_SCALE", std::to_string(scale));
            SDL_Log("[online] this PC's GPU held the game at %u FPS: the session starts at scale %d",
                unsigned(pace.fps), scale);
        }
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

    void App::LoadOnlineFields()
    {
        auto& room = m_online;
        if (room.fieldsFor == m_s->game.id)
            return;
        room.fieldsFor = m_s->game.id;
        const auto settings = online::ReadGameOnline(m_config, m_s->game.id);
        fmt::format_to_n(room.port, sizeof room.port - 1, "{}", settings.port).out[0] = '\0';
        fmt::format_to_n(room.address, sizeof room.address - 1, "{}", settings.address).out[0] = '\0';
        room.automatic = settings.automatic;
        room.crossPlatform = settings.crossPlatform;
        room.showAddress = false;
    }

    void App::SaveOnlineFields()
    {
        auto& room = m_online;
        auto settings = online::ReadGameOnline(m_config, m_s->game.id);
        const unsigned long port = std::strtoul(room.port, nullptr, 10);
        if (port >= 1 && port <= 65535)
            settings.port = uint16_t(port);
        settings.automatic = room.automatic;
        settings.address = room.address;
        settings.crossPlatform = room.crossPlatform;
        online::WriteGameOnline(m_config, m_s->game.id, settings);
        if (!SaveConfig())
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
    }

    void App::OpenOnlineTab()
    {
        m_gameTab = GameTab::Online;
        GoTo(Page::GameSettings);
    }

    void App::DrawGameOnline()
    {
        using online::Mode;
        auto& room = m_online;
        LoadOnlineFields();
        auto settings = online::ReadGameOnline(m_config, m_s->game.id);
        ui::SectionHeader(Tr("Online"), Tr("Play with a friend over the internet, each on your own computer."), false);
        if (ui::BeginSettings("##onlinemode"))
        {
            ui::SettingRow(Tr("How PLAY starts the game"));
            const char* modes[] = { Tr("Solo"), Tr("Host"), Tr("Join") };
            int mode = int(settings.mode);
            if (ui::Segmented("##onlinemodes", modes, &mode))
            {
                settings.mode = Mode(mode);
                online::WriteGameOnline(m_config, m_s->game.id, settings);
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            }
            ui::EndSettings();
        }

        const float width = ImGui::GetContentRegionAvail().x;
        if (settings.mode == Mode::Solo)
        {
            ui::Text(ui::TextStyle::Body,
                Tr("You play on this computer. To play with a friend over the internet, choose Host to open a room "
                   "for them, or Join to enter theirs."),
                ui::color::InkSoft, width);
        }
        else
        {
            ui::SectionHeader(Tr("How it works"));
            if (settings.mode == Mode::Host)
            {
                ui::GuideStep(1, Tr("Press HOST A ROOM"), Tr("On the game's page, in place of PLAY."), width, true);
                ui::GuideStep(2, Tr("Invite your friend"),
                    Tr("In the room, copy your room code and send it to them by message."), width);
                ui::GuideStep(3, Tr("Your friend joins"),
                    Tr("They choose Join in this tab, paste your room code, then press JOIN A ROOM."), width);
                ui::GuideStep(4, Tr("Get ready and start"),
                    Tr("You both choose your figures and press I'm ready. Then you press Start."), width);
            }
            else
            {
                ui::GuideStep(
                    1, Tr("Get the room code"), Tr("Your friend opens a room and sends you its code."), width, true);
                ui::GuideStep(2, Tr("Paste it below"), Tr("Paste the whole code in Room code."), width);
                ui::GuideStep(3, Tr("Press JOIN A ROOM"), Tr("On the game's page, in place of PLAY."), width);
                ui::GuideStep(4, Tr("Get ready"),
                    Tr("Choose your figures and press I'm ready. Your friend starts the game."), width);
            }
            ui::Text(ui::TextStyle::Caption,
                Tr("You both need this game with the same disc edition, and the same launcher version."),
                ui::color::InkSoft, width);

            ui::SectionHeader(settings.mode == Mode::Host ? Tr("Your room") : Tr("Your friend's room"));
            bool changed = false;
            if (ui::BeginSettings("##onlinefields"))
            {
                if (settings.mode == Mode::Host)
                {
                    ui::SettingRow(Tr("Open my router automatically"),
                        Tr("Lets friends outside your home in. Most routers allow it."));
                    changed |= ui::Toggle("##onlineautomatic", &room.automatic);
                }
                else
                {
                    ui::SettingRow(Tr("Room code"), Tr("The whole code your friend sent you."));
                    const float show = ui::ButtonWidth(room.showAddress ? Tr("Hide") : Tr("Show"));
                    changed |= ui::TextField("##onlineaddress", room.address, sizeof room.address,
                        { .hint = "XXXX-XXXX-XXXX-XXXX-XXXX",
                            .width = ImGui::GetContentRegionAvail().x - show - ui::space::S,
                            .flags = room.showAddress ? 0 : ImGuiInputTextFlags_Password });
                    ImGui::SameLine(0, ui::space::S);
                    if (ui::Button(room.showAddress ? Tr("Hide") : Tr("Show")))
                        room.showAddress = !room.showAddress;
                }
                ui::SettingRow(Tr("Cross-platform sessions"),
                    Tr("Play with a friend on another system. Not tested yet: both must turn it on."));
                changed |= ui::Toggle("##onlinecrossplatform", &room.crossPlatform);
                ui::EndSettings();
            }
            if (changed)
                SaveOnlineFields();

            ui::ButtonOptions details;
            details.kind = ui::ButtonKind::Ghost;
            details.trailing = room.showDetails ? ui::Icon::ChevronUp : ui::Icon::ChevronDown;
            if (ui::Button(Tr("Details for your router"), details))
                room.showDetails = !room.showDetails;
            if (room.showDetails)
            {
                if (settings.mode == Mode::Host && ui::BeginSettings("##onlineport"))
                {
                    ui::SettingRow(Tr("Port"), Tr("Change it only if another program uses it."));
                    if (ui::TextField("##onlineportfield", room.port, sizeof room.port,
                            { .flags = ImGuiInputTextFlags_CharsDecimal }))
                        SaveOnlineFields();
                    ui::EndSettings();
                }
                const unsigned long port = std::strtoul(room.port, nullptr, 10);
                const std::vector<std::string> lines =
                    settings.mode == Mode::Host
                        ? std::vector<std::string>{ Tf("The room uses port {}, for UDP.", port),
                              Tr("Open my router automatically asks your router with UPnP or NAT-PMP."),
                              Tf("If your router refuses, open (forward) port {} for UDP to this computer in "
                                 "your router's settings.",
                                  port),
                              Tr("No access to the router? Both install the same free virtual network, such as "
                                 "Tailscale "
                                 "or ZeroTier, then make its code in the room's Details."),
                              Tr("The room code holds your public address, found with a free STUN server. Share it "
                                 "only with your friend.") }
                        : std::vector<std::string>{
                              Tr("The room code holds your friend's address and opens their room."),
                              Tr("In the same home, ask your friend for the code under How to fix it in their room."),
                              Tr("With Tailscale or ZeroTier, ask your friend to make a code in their room's "
                                 "Details.")
                          };
                for (const auto& line : lines)
                    ui::Text(ui::TextStyle::Body, line.c_str(), ui::color::InkSoft, width);
            }
        }
    }

    bool App::RoomOpen() const
    {
        const auto state = m_online.lobby.Get().state;
        return !m_online.launched && !m_online.game.empty() && !m_online.preview &&
               (state == online::State::Waiting || state == online::State::InRoom);
    }

    void App::StartOnline()
    {
        LoadOnlineFields();
        const auto settings = online::ReadGameOnline(m_config, m_s->game.id);
        if (settings.mode == online::Mode::Join && settings.address.empty())
        {
            Toast(Tr("Paste your friend's room code first."));
            OpenOnlineTab();
            return;
        }
        m_online.mode = settings.mode;
        if (OpenOnline())
            BeginRoom();
    }

    bool App::BeginRoom()
    {
        using namespace online;
        auto& room = m_online;
        room.error.clear();
        room.showFix = false;
        room.cardHeight = 0;
        room.loggedReach = -1;
        room.otherAddress[0] = '\0';
        const bool hosting = room.mode == Mode::Host;
        const auto* game = collection::Catalogue::Get().FindGame(m_s->game.id);
        uint16_t port = 0;
        std::string host;
        if (hosting && !net::ParseAddress(std::string("127.0.0.1:") + room.port, DefaultPort, host, port))
        {
            room.error = Tr("This port isn't valid; choose a number from 1 to 65535.");
            return false;
        }
        uint64_t secret = 0;
        if (!hosting && !DecodeRoomCode(room.address, host, port, secret))
        {
            room.lobby.Join({ room.address, {}, {} });
            return false;
        }
        if (!game || room.recovering || !DiscStillThere() || !m_session.Running().empty())
            return false;
        room.game = m_s->game.id;
        room.realSaves = FoldersOf(room.game).saves;
        room.inputs = ReadRoomInputs(
            m_config, m_profiles.Active(), *game, m_s->check.xexSha256, hosting, SessionFile(*game), room.realSaves);
        if (room.inputs.error != InputError::None)
        {
            room.error = online_page::InputError(room.inputs.error);
            return false;
        }
        if (hosting)
        {
            HostOptions options;
            options.port = port;
            options.discover = m_shots.empty();
            options.hello = room.inputs.hello;
            options.player = room.inputs.player;
            options.settings = room.inputs.settings;
            options.save = room.inputs.save;
            options.secret = room.keepSecret ? room.lastSecret : 0;
            room.keepSecret = false;
            if (!room.lobby.Host(std::move(options)))
                return false;
            room.lastSecret = room.lobby.Secret();
            room.localAddress = online_page::LocalAddress();
            if (m_shots.empty())
                room.router.Open(port, room.automatic);
            return true;
        }
        room.joinedHost = host;
        room.joinedPort = port;
        return room.lobby.Join({ room.address, room.inputs.hello, room.inputs.player, m_shots.empty() });
    }

    bool App::DrawOnlineHelp(const OnlineHelp& help)
    {
        using Action = OnlineHelp::Action;
        ui::BeginCard("##onlinehelp", ImVec2(ImGui::GetContentRegionAvail().x, 0),
            { .inset = true,
                .padding = ui::space::M,
                .flags = ImGuiChildFlags_AutoResizeY,
                .edge = ui::color::Warning });
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ui::DrawIcon(ImGui::GetWindowDrawList(), ui::Icon::Warning,
            ImVec2(at.x + ui::icon::Small / 2, at.y + ui::CapMiddle(ui::TextStyle::Label, true)), ui::icon::Small,
            ui::Col(ui::color::Warning));
        ImGui::SetCursorScreenPos(ImVec2(at.x + ui::icon::Small + ui::space::S, at.y));
        ui::Text(
            ui::TextStyle::Label, help.title.c_str(), ui::color::Ink, width - ui::icon::Small - ui::space::S, true);
        for (const auto& line : help.lines)
            ui::Text(ui::TextStyle::Body, line.c_str(), ui::color::InkSoft, width);
        const char* label = help.action == Action::Retry        ? Tr("Try again")
                            : help.action == Action::Settings   ? Tr("Open the Online tab")
                            : help.action == Action::Update     ? Tr("Go to the game's page")
                            : help.action == Action::Launcher   ? Tr("Check for updates")
                            : help.action == Action::Collection ? Tr("Choose figures")
                                                                : nullptr;
        bool pressed = false;
        if (label != nullptr)
        {
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            pressed = ui::Button(label, { .kind = ui::ButtonKind::Primary });
            TakeFocus(FocusTo::Page);
        }
        ui::EndCard();
        if (!pressed || m_online.preview)
            return false;
        switch (help.action)
        {
        case Action::Retry:
            m_online.lobby.Leave();
            m_online.router.Close();
            BeginRoom();
            break;
        case Action::Settings:
            CloseOnline();
            OpenOnlineTab();
            break;
        case Action::Update:
            CloseOnline();
            GoTo(Page::Play);
            break;
        case Action::Launcher:
            CloseOnline();
            m_tab = SettingsTab::Launcher;
            GoTo(Page::Settings);
            break;
        case Action::Collection: GoTo(Page::Collection); break;
        case Action::None: break;
        }
        return true;
    }

    void App::DrawRoomInvite(const online::Status& status)
    {
        auto& room = m_online;
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float size = ui::height::Large;
        ui::DrawEmptySeat(ImGui::GetWindowDrawList(), at, size);
        ImGui::SetCursorScreenPos(
            ImVec2(at.x + size + ui::space::M, at.y + (size - ui::SizeOf(ui::TextStyle::Label)) / 2));
        ui::Text(ui::TextStyle::Label, Tr("Waiting for player 2"), ui::color::Ink, width - size - ui::space::M, true);
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + size + ui::space::M));
        if (!status.host)
            return;
        using Reach = Invite::Reach;
        const uint16_t port = uint16_t(std::strtoul(room.port, nullptr, 10));
        const bool previewing = room.preview && room.previewRouter >= 0;
        const auto mapping = previewing ? netplay::portmap::Mapping{ .ok = room.previewRouter == 1,
            .externalIp = room.previewRouter == 1 ? "203.0.113.4" : "",
            .externalPort = port }
                                        : room.router.Get();
        const auto seen = previewing
                              ? online::stun::Result{ { room.previewRouter == 1 ? "203.0.113.4" : "198.51.100.7", 0 },
                                    { room.previewRouter == 3 ? "2001:db8::7" : "", 0 } }
                              : status.mapped;
        const bool pending = room.preview ? room.previewRouter == 0 : room.router.Pending();
        const Invite invite =
            InviteFor(mapping, seen, pending, room.automatic, room.localAddress, port, room.lobby.Secret());
        if (int(invite.reach) != room.loggedReach && !room.preview)
        {
            room.loggedReach = int(invite.reach);
            static constexpr const char* Reaches[] = { "asking the router", "a friend anywhere (router opened)",
                "a friend with IPv6 (router not opened)", "home only (address shared by the provider)",
                "home only (router refused)", "home, or a forwarded port (automatic opening off)" };
            SDL_Log("[online] room code: %s, %s", invite.code.empty() ? "not ready" : "ready",
                Reaches[size_t(invite.reach)]);
        }
        if (invite.code.empty())
        {
            ui::Text(ui::TextStyle::Caption, Tr("Getting your room code..."), ui::color::InkSoft, width);
            return;
        }
        ui::Text(ui::TextStyle::Body, Tr("Send your friend this room code:"), ui::color::InkSoft, width);
        const std::string shown = room.showAddress ? invite.code : MaskedCode(invite.code);
        // The code on a line of its own, never broken (a player reads it out or copies it whole), the buttons under it.
        ui::Text(ui::TextStyle::Label, shown.c_str(), ui::color::Ink, -1, true);
        if (ui::Button(Tr("Copy"), { .size = ui::Size::Small }))
        {
            SDL_SetClipboardText(invite.code.c_str());
            Toast(Tr("Room code copied. Paste it in a message to your friend."));
        }
        TakeFocus(FocusTo::Page);
        ImGui::SameLine(0, ui::space::XS);
        if (ui::Button(
                room.showAddress ? Tr("Hide") : Tr("Show"), { .kind = ui::ButtonKind::Ghost, .size = ui::Size::Small }))
            room.showAddress = !room.showAddress;
        switch (invite.reach)
        {
        case Reach::Anywhere:
            ui::Text(
                ui::TextStyle::Caption, Tr("Your router lets friends in from anywhere."), ui::color::Success, width);
            break;
        case Reach::Ipv6:
            ui::Text(ui::TextStyle::Caption,
                Tr("Your router didn't open, so this code uses IPv6. It works if your friend's internet has IPv6 "
                   "too."),
                ui::color::Warning, width);
            break;
        case Reach::Shared:
            ui::Text(ui::TextStyle::Caption,
                Tr("Your internet provider shares your address with other homes, so a friend elsewhere can't reach "
                   "you this way."),
                ui::color::Warning, width);
            break;
        case Reach::Refused:
            ui::Text(ui::TextStyle::Caption,
                Tr("Your router didn't let your friend in by itself, so a friend elsewhere can't join yet."),
                ui::color::Warning, width);
            break;
        case Reach::Manual:
            ui::Text(ui::TextStyle::Caption,
                Tr("Your router isn't opened automatically: a friend elsewhere needs the port opened."),
                ui::color::Warning, width);
            break;
        case Reach::Asking: return;
        }
        ui::ButtonOptions fix;
        fix.kind = ui::ButtonKind::Ghost;
        fix.size = ui::Size::Small;
        fix.trailing = room.showFix ? ui::Icon::ChevronUp : ui::Icon::ChevronDown;
        if (invite.reach != Reach::Anywhere && ui::Button(Tr("How to fix it"), fix))
            room.showFix = !room.showFix;
        if (room.showFix)
        {
            if (invite.reach == Reach::Refused || invite.reach == Reach::Manual)
            {
                const std::string forward =
                    Tf("Open port {} (UDP) to this computer in your router's settings: search the web for "
                       "your router's name and \"port forwarding\".",
                        room.port);
                ui::Text(ui::TextStyle::Body, forward.c_str(), ui::color::InkSoft, width);
            }
            ui::Text(ui::TextStyle::Body,
                Tr("No open port? Both of you can join one virtual network (Radmin VPN, ZeroTier or Tailscale), then "
                   "make a code for this computer's address there in Details below."),
                ui::color::InkSoft, width);
            if (!invite.homeCode.empty() && invite.homeCode != invite.code)
            {
                const std::string home = Tf("A friend in your home can use this code instead: {}",
                    room.showAddress ? invite.homeCode : MaskedCode(invite.homeCode));
                ui::Text(ui::TextStyle::Body, home.c_str(), ui::color::InkSoft, width);
            }
        }
        if (ui::Button(Tr("Details"), { .kind = ui::ButtonKind::Ghost, .size = ui::Size::Small }))
            room.showDetails = !room.showDetails;
        if (room.showDetails)
        {
            ui::Text(ui::TextStyle::Label, Tr("Code for another address"), ui::color::Ink, width);
            ui::Text(ui::TextStyle::Caption,
                Tr("Enter this computer's address on Radmin VPN, ZeroTier, Tailscale or playit.gg, with its port."),
                ui::color::InkSoft, width);
            ui::TextField(
                "##otheraddress", room.otherAddress, sizeof room.otherAddress, { .hint = "100.64.0.2:47640" });
            room.lobby.ResolveCode(room.otherAddress, port);
            const std::string code = room.lobby.OtherCode();
            if (!code.empty())
            {
                const std::string shownCode = room.showAddress ? code : MaskedCode(code);
                ui::Text(ui::TextStyle::Body, shownCode.c_str(), ui::color::Ink,
                    std::max(40.0f, width - ui::ButtonWidth(Tr("Copy")) - ui::space::S));
                ImGui::SameLine(0, ui::space::S);
                ImGui::PushID("othercode");
                if (ui::Button(Tr("Copy"), { .size = ui::Size::Small }))
                {
                    SDL_SetClipboardText(code.c_str());
                    Toast(Tr("Room code copied. Paste it in a message to your friend."));
                }
                ImGui::PopID();
            }
        }
    }

    void App::DrawRoomPlayers(const online::Status& status)
    {
        using namespace online;
        auto& room = m_online;
        const Player& p1 = status.host ? room.inputs.player : status.remote;
        const Player& p2 = status.host ? status.remote : room.inputs.player;
        const float playerWidth = (ImGui::GetContentRegionAvail().x - ui::space::M) / 2;
        const float minHeight = room.cardHeight;
        float tallest = 0;
        for (int i = 0; i < 2; ++i)
        {
            const bool local = i == (status.host ? 0 : 1);
            const bool present = local || status.haveRemote;
            const Player& player = i == 0 ? p1 : p2;
            ImGui::PushID(i);
            ui::BeginCard("##player", ImVec2(playerWidth, minHeight),
                { .inset = true,
                    .padding = ui::space::M,
                    .flags = minHeight > 0 ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY });
            const float top = ImGui::GetCursorPosY();
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
                ImDrawList* draw = ImGui::GetWindowDrawList();
                draw->AddCircleFilled(ImVec2(at.x + size / 2, at.y + size / 2), size / 2, ui::Col(color));
                if (room.pictures[i])
                    draw->AddImageRounded(Ref(room.pictures[i]), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0),
                        ImVec2(1, 1), IM_COL32_WHITE, size / 2);
                else
                {
                    const std::string initials = profiles::Initials(player.pseudo);
                    ui::PushText(ui::TextStyle::Label, true);
                    const ImVec2 text = ImGui::CalcTextSize(initials.c_str());
                    ui::DrawText(draw,
                        ImVec2(at.x + (size - text.x) / 2, at.y + size / 2 - ui::CapMiddle(ui::TextStyle::Label, true)),
                        ui::Col(ui::InkOn(color, ui::color::Panel)), initials.c_str());
                    ui::PopText();
                }
                ImGui::Dummy(ImVec2(size, size));
                ImGui::SameLine(0, ui::space::M);
                ImGui::BeginGroup();
                // Both players may keep the default name: the local card says which one is you.
                const std::string name = local ? Tf("{} (you)", player.pseudo) : player.pseudo;
                ui::Text(ui::TextStyle::Label, name.c_str(), ui::color::Ink, ImGui::GetContentRegionAvail().x, true);
                const bool ready = local ? status.localReady : status.remoteReady;
                const char* role = i == 0 ? Tr("Player 1 · Host") : Tr("Player 2 · Guest");
                ui::Text(ui::TextStyle::Caption, role, ui::color::InkSoft);
                if (ready)
                {
                    ImGui::SameLine(0, ui::space::S);
                    ui::Badge(Tr("Ready"), ui::color::Success);
                }
                ImGui::EndGroup();
                std::string figures;
                for (const auto& figure : player.figures)
                {
                    if (!figures.empty())
                        figures += " · ";
                    figures += figure.name;
                }
                ImGui::Dummy(ImVec2(0, ui::space::XS));
                const std::string onPortal = figures.empty() ? std::string(Tr("No figures on the portal yet"))
                                                             : Tf("On the portal: {}", figures);
                ui::Text(ui::TextStyle::Body, onPortal.c_str(),
                    figures.empty() ? ui::color::Warning : ui::color::InkSoft, ImGui::GetContentRegionAvail().x);
                if (local)
                {
                    ImGui::Dummy(ImVec2(0, ui::space::XS));
                    const bool inRoom = status.state == State::InRoom;
                    if (inRoom)
                    {
                        if (ui::Button(ready ? Tr("Not ready") : Tr("I'm ready"),
                                { .kind = ready ? ui::ButtonKind::Secondary : ui::ButtonKind::Primary }) &&
                            !room.preview)
                            room.lobby.SetReady(!ready);
                        TakeFocus(FocusTo::Page);
                    }
                    // No SameLine left open after the last button: the card's height is measured below it.
                    if (!ready)
                    {
                        // Beside the first button when it fits, else under it (a narrow window).
                        const float choose = ui::ButtonWidth(Tr("Choose figures"));
                        if (inRoom && ImGui::GetItemRectMax().x + ui::space::S + choose <=
                                          ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
                            ImGui::SameLine(0, ui::space::S);
                        if (ui::Button(Tr("Choose figures")) && !room.preview)
                            GoTo(Page::Collection);
                    }
                }
                else if (!ready)
                    ui::Text(ui::TextStyle::Caption, Tr("Not ready yet: choosing their figures"), ui::color::InkSoft);
            }
            else
                DrawRoomInvite(status);
            tallest = std::max(tallest, ImGui::GetCursorPosY() - top);
            ui::EndCard();
            ImGui::PopID();
            if (i == 0)
                ImGui::SameLine(0, ui::space::M);
        }
        room.cardHeight = tallest + 2 * ui::space::M;
    }

    void App::DrawOnline()
    {
        using namespace online;
        auto& room = m_online;
        const Status status = room.preview ? *room.preview : room.lobby.Get();
        BeginSettingsPage("##onlinecolumn", Tr("Online"), true, true);
        BeginCard("##online", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        const float width = ImGui::GetContentRegionAvail().x;
        const bool idle = status.state == State::Idle || status.state == State::Ended;
        const std::string inputError = room.error;
        if (idle)
        {
            ui::SectionHeader(room.mode == Mode::Host ? Tr("Your room") : Tr("Your friend's room"), nullptr, false);
            OnlineHelp help;
            if (!inputError.empty())
                help = { Tr("The room couldn't open"), { inputError }, OnlineHelp::Action::Retry };
            else if (status.error != Error::None)
                help = OnlineHelpFor(status.error, status.mismatch, room.inputs.hello, status.remoteHello);
            else
                help = { Tr("The room is closed"), { Tr("Open or join a room again to play.") },
                    OnlineHelp::Action::Retry };
            DrawOnlineHelp(help);
            ImGui::Dummy(ImVec2(0, ui::space::S));
            if (ui::Button(Tr("Back")))
            {
                CloseOnline();
                GoTo(Page::Play);
            }
        }
        else if (status.state == State::Connecting || status.state == State::Greeting)
        {
            ui::SectionHeader(Tr("Your friend's room"), nullptr, false);
            ui::Spinner();
            ImGui::SameLine(0, ui::space::S);
            ui::Text(ui::TextStyle::Body, Tr("Trying to reach your friend's room..."), ui::color::Ink, width);
            ImGui::Dummy(ImVec2(0, ui::space::S));
            if (ui::Button(Tr("Cancel")))
            {
                CloseOnline();
                GoTo(Page::Play);
            }
            TakeFocus(FocusTo::Page);
        }
        else
        {
            const Settings& settings = status.host ? room.inputs.settings : status.settings;
            std::string line;
            if (status.host || status.haveSettings)
            {
                auto language = [](const std::string& code) {
                    for (const auto& choice : AllLanguageChoices)
                        if (code == choice.value)
                            return std::string(Tr(choice.label));
                    return code.empty() ? std::string(Tr("Game default")) : code;
                };
                line = Tf("{} FPS · Text: {} · Voices: {}", settings.fpsLimit, language(settings.language),
                    settings.voices.empty() ? std::string(Tr("Same as text")) : language(settings.voices));
                if (settings.nightmareFromStart)
                    line += std::string(" · ") + Tr("Nightmare from the start");
            }
            ui::SectionHeader(
                status.host ? Tr("Your room") : Tr("Your friend's room"), line.empty() ? nullptr : line.c_str(), false);
            DrawRoomPlayers(status);
            if (status.haveRemote && status.rttMs >= 0 && !status.testDone)
                ui::Text(
                    ui::TextStyle::Body, Tr("Testing the connection with your friend..."), ui::color::InkSoft, width);
            else if (status.haveRemote && status.rttMs >= 0)
            {
                // The connection test's verdict once it is over, the live round trip after.
                const double measured = status.testRttMs >= 0 ? status.testRttMs : status.rttMs;
                const int verdict = ConnectionVerdict(measured, status.testJitterMs);
                const int ms = int(std::lround(status.rttMs));
                const char* quality = verdict == 0   ? Tr("perfect")
                                      : verdict == 1 ? Tr("playable")
                                      : verdict == 2 ? Tr("slow")
                                                     : Tr("very slow");
                const std::string timing =
                    status.inputDelay == 1
                        ? Tf("Connection: {} · {} ms · input delay 1 frame", quality, ms)
                        : Tf("Connection: {} · {} ms · input delay {} frames", quality, ms, status.inputDelay);
                ui::Text(
                    ui::TextStyle::Body, timing.c_str(), verdict <= 1 ? ui::color::InkSoft : ui::color::Warning, width);
                if (status.testJitterMs >= 30)
                {
                    const std::string jitter = Tf("The round trip varies by {} ms: the input delay grows to absorb it.",
                        int(std::lround(status.testJitterMs)));
                    ui::Text(ui::TextStyle::Body, jitter.c_str(), ui::color::Warning, width);
                }
                // Past 250 ms the game feels slow to answer: said plainly, the start left to the players.
                if (verdict == 3)
                    ui::Text(ui::TextStyle::Body,
                        Tr("This connection is very slow: the game will be slow to answer your buttons."),
                        ui::color::Warning, width);
                // Both games go at the slower PC's pace: said when one of them is known to stay under 28 FPS.
                const Player& mine = room.inputs.player;
                const Player& theirs = status.remote;
                if (mine.fps != 0 && theirs.fps != 0 && std::min(mine.fps, theirs.fps) < 28)
                {
                    const std::string pace =
                        mine.fps <= theirs.fps
                            ? Tf("Your PC played this game at about {} FPS: the session will go at that pace.",
                                  unsigned(mine.fps))
                            : Tf("{}'s PC played this game at about {} FPS: the session will go at that pace.",
                                  theirs.pseudo, unsigned(theirs.fps));
                    ui::Text(ui::TextStyle::Body, pace.c_str(), ui::color::Warning, width);
                }
            }
            if (!inputError.empty())
                DrawOnlineHelp({ Tr("Something needs fixing"), { inputError },
                    inputError == online_page::InputError(InputError::Figure) ? OnlineHelp::Action::Collection
                                                                              : OnlineHelp::Action::None });
            if (status.lastRefusal.error != Error::None)
            {
                OnlineHelp refused =
                    OnlineHelpFor(status.lastRefusal.error, status.lastRefusal.mismatch, room.inputs.hello, {});
                refused.title = Tf("A player couldn't join: {}", refused.title);
                refused.action = OnlineHelp::Action::None;
                DrawOnlineHelp(refused);
            }
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            const std::string& friendName = status.remote.pseudo;
            std::string waiting;
            if (!status.haveRemote)
                waiting = Tr("Waiting for player 2 to join.");
            else if (!status.localReady)
                waiting = Tr("Press I'm ready when your figures are set.");
            else if (!status.remoteReady)
                waiting = Tf("Waiting for {} to be ready.", friendName);
            else if (status.host && !status.CanStart())
                waiting = Tf("Sending your save to {}...", friendName);
            else
                waiting =
                    status.host ? Tr("Everyone is ready: press Start.") : Tr("Everyone is ready: the host starts.");
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
            ImGui::SameLine(0, ui::space::M);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ui::height::Normal - ui::SizeOf(ui::TextStyle::Body)) / 2);
            ui::Text(ui::TextStyle::Body, waiting.c_str(), ui::color::InkSoft, ImGui::GetContentRegionAvail().x);
        }
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
        constexpr int MismatchCount = int(Mismatch::Platform) + 1;
        constexpr const char* endReasons[] = { "quit", "timeout", "desync", "transport", "protocol", "game", "image",
            "runtime", "platform", "fps", "seats", "invalid" };
        constexpr int Rooms = 9, Inputs = 5, Extras = 5;
        if (m_testStep >= Rooms + ErrorCount + MismatchCount + Inputs + Extras + int(std::size(endReasons)))
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
        room.mode = Mode::Host;
        room.previewRouter = -1;
        room.showFix = room.showDetails = false;
        room.localAddress = "192.0.2.10";
        room.inputs.player = { "Spyro", "E000000000000001", 4, {},
            { { 16, 0, 0, "Spyro", std::vector<uint8_t>(FigureDumpBytes, 1) } } };
        room.inputs.settings = { 60, "en", "fr", false, 0, 0 };
        room.inputs.hello = { LobbyProtocolVersion, true, "0.10.0", "ssa", std::string(64, 'a'), "0.9.16",
            "macos-arm64" };
        status.remoteHello = { LobbyProtocolVersion, false, "0.9.10", "giants", std::string(64, 'b'), "0.9.15",
            "windows-x64" };
        const Player kaos = { "Kaos", "E000000000000002", 7, {},
            { { 7, 0, 0, "Gill Grunt", std::vector<uint8_t>(FigureDumpBytes, 2) } } };
        auto setMode = [&](const char* mode) {
            m_config[std::string(m_s->game.id) + ".online"]["mode"] = mode;
            m_config[std::string(m_s->game.id) + ".online"]["address"] =
                online::EncodeRoomCode("203.0.113.4", online::DefaultPort, 0);
            room.fieldsFor.clear();
        };
        int step = m_testStep;
        if (step >= 4)
            setMode("solo"); // only the tab's and the game page's shots show another mode
        if (step < Rooms)
        {
            if (step < 4)
            {
                static const char* const modes[] = { "solo", "host", "join", "host" };
                static const char* const shots[] = { "online-tab-solo", "online-tab-host", "online-tab-join",
                    "online-tab-details" };
                setMode(modes[step]);
                room.showDetails = step == 3;
                m_page = Page::GameSettings;
                m_gameTab = GameTab::Online;
                m_testShot = shots[step];
            }
            else if (step < 6)
            {
                status.host = true;
                status.state = State::Waiting;
                room.previewRouter = step == 4 ? 2 : 1;
                room.showFix = step == 4;
                m_testShot = step == 4 ? "online-02-waiting" : "online-waiting-open";
            }
            else if (step < 8)
            {
                status.host = step == 6;
                room.mode = status.host ? Mode::Host : Mode::Join;
                status.state = State::InRoom;
                status.haveRemote = true;
                status.remote = kaos;
                status.localReady = step == 6;
                status.remoteReady = true;
                status.haveSettings = true;
                status.settings = room.inputs.settings;
                status.rttMs = step == 6 ? 42 : 180;
                status.inputDelay = step == 6 ? 3 : 1;
                m_testShot = step == 6 ? "online-03-ready" : "online-guest";
            }
            else
            {
                room.mode = Mode::Join;
                status.state = State::Connecting;
                m_testShot = "online-connecting";
            }
        }
        else if ((step -= Rooms) < ErrorCount)
        {
            status.state = State::Ended;
            status.error = Error(step + 1);
            room.mode = step % 2 ? Mode::Join : Mode::Host;
            m_testShot = fmt::format("online-error-{:02}", step + 1);
        }
        else if ((step -= ErrorCount) < MismatchCount)
        {
            status.state = State::Ended;
            status.error = Error::Mismatch;
            status.mismatch = Mismatch(step);
            m_testShot = fmt::format("online-mismatch-{:02}", step);
        }
        else if ((step -= MismatchCount) < Inputs)
        {
            const int inputError = step + 1;
            if (inputError <= 4)
                room.error = online_page::InputError(InputError(inputError));
            else
                room.error = Tr("Couldn't start the online game; check your game installation and try again.");
            m_testShot = fmt::format("online-input-{:02}", inputError);
        }
        else if ((step -= Inputs) < Extras)
        {
            if (step == 0)
            {
                status.host = true;
                status.state = State::Waiting;
                room.previewRouter = 1;
                status.lastRefusal.error = Error::WrongCode;
                m_testShot = "online-refusal";
            }
            else if (step < 3)
            {
                setMode(step == 1 ? "host" : "join");
                m_page = Page::Play;
                m_s->disc = DiscState::Ready;
                m_s->installed.runtime = "display-only";
                m_s->skipUpdate = true;
                m_testShot = step == 1 ? "online-game-host" : "online-game-join";
            }
            else
            {
                room.error =
                    step == 3
                        ? Tr("This port isn't valid; choose a number from 1 to 65535.")
                        : Tr("Couldn't restore the session's saves or figures; check the data folder and restart.");
                m_testShot = step == 3 ? "online-invalid-port" : "online-recovery-error";
            }
        }
        else
        {
            step -= Extras;
            setMode("solo");
            m_page = Page::Play;
            m_s->disc = DiscState::Ready;
            m_s->installed.runtime = "display-only";
            m_s->skipUpdate = true;
            const char* reason = endReasons[step];
            if (const std::string text = OnlineEndText(reason); !text.empty())
                Toast(text, ui::ToastKind::Error);
            m_testShot = fmt::format("online-ended-{}", reason);
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
