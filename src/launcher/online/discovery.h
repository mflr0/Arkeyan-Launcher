#pragma once

#include "launcher/online/net_socket.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace online
{
    class Discovery
    {
    public:
        using Resolve = std::function<std::vector<net::Endpoint>(const std::string&, uint16_t)>;
        explicit Discovery(Resolve resolve = {});
        ~Discovery();
        void Start(bool discover = true);
        void Close();
        std::vector<net::Endpoint> StunServers();
        // DNS stays off the window thread; a revision discards answers for an edited field.
        void ResolveTunnel(std::string address, uint16_t port);
        net::Endpoint Tunnel();

    private:
        void Work();
        Resolve m_resolve;
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::thread m_thread;
        bool m_quit = false, m_active = false;
        uint64_t m_generation = 0, m_codeRevision = 0;
        uint16_t m_codePort = 0;
        std::string m_codeAddress;
        net::Endpoint m_tunnel;
        std::vector<net::Endpoint> m_servers;
    };
}
