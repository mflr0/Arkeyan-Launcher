#pragma once

#include "launcher/online/room_inputs.h"
#include "launcher/online/session_launch.h"
#include "launcher/online/stun.h"
#include "launcher/online/local_addresses.h"
#include "shared/portmap.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

struct SDL_Texture;

namespace launcher_ui
{
    class RouterLease
    {
    public:
        using MapFunction = std::function<netplay::portmap::Mapping(const netplay::portmap::Options&)>;
        using UnmapFunction = std::function<void(const netplay::portmap::Mapping&, const netplay::portmap::Options&)>;
        using LookupFunction = std::function<online::stun::Result()>;
        explicit RouterLease(MapFunction map = netplay::portmap::Map, UnmapFunction unmap = netplay::portmap::Unmap,
            std::chrono::milliseconds renewal = std::chrono::hours(1), LookupFunction lookup = {})
            : m_map(std::move(map)), m_unmap(std::move(unmap)), m_lookup(std::move(lookup)), m_renewal(renewal)
        {}
        ~RouterLease();
        // The lobby owns STUN so discovery and the room use the same UDP socket.
        void Open(uint16_t port, bool map = true);
        void Close();
        netplay::portmap::Mapping Get();
        online::stun::Result Seen();
        bool Pending();

    private:
        void Work();
        MapFunction m_map;
        UnmapFunction m_unmap;
        LookupFunction m_lookup;
        std::chrono::milliseconds m_renewal;
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::thread m_thread;
        bool m_quit = false, m_active = false, m_pending = false, m_mapPort = true;
        uint64_t m_generation = 0;
        uint16_t m_port = online::DefaultPort;
        netplay::portmap::Mapping m_mapping;
        online::stun::Result m_seen;
    };

    // What the host's room tells the friend: the room code, the address it holds, and how far it reaches.
    struct Invite
    {
        enum class Reach
        {
            Asking,   // the router or the public address is not known yet
            Anywhere, // the router opened the port on a public IPv4 address
            Ipv6,     // the router did not open it: the code holds this computer's IPv6 address
            Shared,   // the router opened it, but the provider shares one address among homes (CGNAT, two routers)
            Refused,  // the router refused
            Manual,   // automatic opening is off: the player forwards the port
        };
        Reach reach = Reach::Asking;
        std::string code, address;
        std::string paths;
        std::vector<online::net::Endpoint> endpoints;
    };
    Invite InviteFor(const netplay::portmap::Mapping& mapping, const online::stun::Result& seen, bool pending,
        bool automatic, std::span<const online::LocalAddress> localAddresses, uint16_t port, uint64_t secret,
        const online::net::Endpoint& tunnel = {});

    // What went wrong, said for someone who knows nothing of networks: a title, the facts (each side's version), what to
    // do, and the button that does it.
    struct OnlineHelp
    {
        enum class Action
        {
            None,
            Retry,    // open or join the room again
            Settings, // the game's Online tab
            Update,   // the game's page, where its update is offered
            Launcher, // Settings > Launcher, where the launcher's update is checked
            Collection,
        };
        std::string title;
        std::vector<std::string> lines;
        Action action = Action::None;
    };
    OnlineHelp OnlineHelpFor(
        online::Error error, online::Mismatch mismatch, const online::Hello& mine, const online::Hello& theirs);

    struct OnlineRoom
    {
        online::Lobby lobby;
        RouterLease router;
        online::RoomInputs inputs;
        online::Mode mode = online::Mode::Host; // of the room open now
        std::string fieldsFor; // the game whose Online tab settings fill the fields below
        char port[6] = "47640", address[256] = {}, otherAddress[256] = {};
        bool automatic = true, crossPlatform = false;
        bool showAddress = false, showDetails = false, showFix = false;
        float cardHeight = 0; // the taller player card's, last frame: both are drawn that tall
        int previewRouter = -1; // tests: 0 asking the router, 1 opened, 2 refused, 3 refused with IPv6
        int loggedReach = -1;   // the invite's reach last logged
        // "Play together again": the host reopens its room with the last code; the guest joins again until the host's
        // room answers (rejoinUntil, SDL ticks).
        uint64_t lastSecret = 0, rejoinUntil = 0, rejoinAt = 0;
        bool keepSecret = false;
        uint16_t joinedPort = online::DefaultPort;
        std::string game, joinedHost, error, with;
        std::vector<online::LocalAddress> localAddresses;
        uint64_t addressesAt = 0;
        std::string lastCode;
        std::filesystem::path realSaves;
        uint64_t refreshAt = 0;
        bool launched = false, recovering = false;
        online::State lastState = online::State::Idle;
        std::optional<online::Status> preview;
        SDL_Texture* pictures[2] = {};
        std::vector<uint8_t> pictureBytes[2];
    };

    std::string MaskedAddress(std::string_view address); // dots for the IP, the port kept
    std::string MaskedCode(std::string_view code); // the letters hidden, the dashes kept
    const char* OnlineErrorText(online::Error error, online::Mismatch mismatch);
    std::string OnlineEndText(std::string_view reason);
}
