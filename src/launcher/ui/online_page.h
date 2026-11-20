#pragma once

#include "launcher/online/room_inputs.h"
#include "launcher/online/session_launch.h"
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
        explicit RouterLease(MapFunction map = netplay::portmap::Map, UnmapFunction unmap = netplay::portmap::Unmap,
            std::chrono::milliseconds renewal = std::chrono::hours(1))
            : m_map(std::move(map)), m_unmap(std::move(unmap)), m_renewal(renewal)
        {}
        ~RouterLease();
        void Open(uint16_t port);
        void Close();
        netplay::portmap::Mapping Get();
        bool Pending();

    private:
        void Work();
        MapFunction m_map;
        UnmapFunction m_unmap;
        std::chrono::milliseconds m_renewal;
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::thread m_thread;
        bool m_quit = false, m_active = false, m_pending = false;
        uint64_t m_generation = 0;
        uint16_t m_port = online::DefaultPort;
        netplay::portmap::Mapping m_mapping;
    };

    struct OnlineRoom
    {
        online::Lobby lobby;
        RouterLease router;
        online::RoomInputs inputs;
        int tab = 0;
        char port[6] = "47640", address[256] = {}, password[128] = {};
        bool automatic = true;
        uint16_t joinedPort = online::DefaultPort;
        std::string game, joinedHost, localAddress, error, with;
        std::filesystem::path realSaves;
        uint64_t refreshAt = 0;
        bool launched = false, recovering = false;
        online::State lastState = online::State::Idle;
        std::optional<online::Status> preview;
        SDL_Texture* pictures[2] = {};
        std::vector<uint8_t> pictureBytes[2];
    };

    const char* OnlineErrorText(online::Error error, online::Mismatch mismatch);
    std::string OnlineEndText(std::string_view reason);
}
