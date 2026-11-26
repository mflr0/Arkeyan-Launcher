#include "launcher/online/discovery.h"
#include "launcher/online/stun.h"

namespace online
{
    Discovery::Discovery(Resolve resolve)
        : m_resolve(resolve ? std::move(resolve) : [](const auto& host, auto port) { return net::Resolve(host, port); })
    {
        m_thread = std::thread(&Discovery::Work, this);
    }
    Discovery::~Discovery()
    {
        {
            std::lock_guard lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_one();
        m_thread.join();
    }
    void Discovery::Start(bool discover)
    {
        std::lock_guard lock(m_mutex);
        m_servers.clear();
        m_tunnel = {};
        m_codeAddress.clear();
        m_codePort = 0;
        ++m_codeRevision;
        m_active = discover;
        ++m_generation;
        m_wake.notify_one();
    }
    void Discovery::Close()
    {
        std::lock_guard lock(m_mutex);
        m_active = false;
        m_servers.clear();
        m_tunnel = {};
        m_codeAddress.clear();
        m_codePort = 0;
        ++m_codeRevision;
        ++m_generation;
        m_wake.notify_one();
    }
    std::vector<net::Endpoint> Discovery::StunServers()
    {
        std::lock_guard lock(m_mutex);
        return m_servers;
    }
    void Discovery::ResolveTunnel(std::string address, uint16_t port)
    {
        std::lock_guard lock(m_mutex);
        if (address == m_codeAddress && port == m_codePort)
            return;
        m_codeAddress = std::move(address);
        m_codePort = port;
        m_tunnel = {};
        ++m_codeRevision;
        m_wake.notify_one();
    }
    net::Endpoint Discovery::Tunnel()
    {
        std::lock_guard lock(m_mutex);
        return m_tunnel;
    }

    void Discovery::Work()
    {
        std::unique_lock lock(m_mutex);
        uint64_t generation = 0, codeRevision = 0;
        for (;;)
        {
            m_wake.wait(lock, [&] { return m_quit || generation != m_generation || codeRevision != m_codeRevision; });
            if (m_quit)
                return;
            if (codeRevision != m_codeRevision)
            {
                codeRevision = m_codeRevision;
                const auto address = m_codeAddress;
                const auto port = m_codePort;
                lock.unlock();
                std::string host;
                net::Endpoint tunnel;
                uint16_t to = 0;
                if (net::ParseAddress(address, port, host, to))
                {
                    const auto endpoints = m_resolve(host, to);
                    if (!endpoints.empty())
                        tunnel = endpoints.front();
                }
                lock.lock();
                if (codeRevision == m_codeRevision)
                    m_tunnel = std::move(tunnel);
            }
            if (generation == m_generation)
                continue;
            generation = m_generation;
            if (!m_active)
                continue;
            lock.unlock();
            std::vector<net::Endpoint> servers;
            for (const auto server : stun::Servers)
            {
                {
                    std::lock_guard check(m_mutex);
                    if (m_quit || generation != m_generation)
                        break;
                }
                std::string name;
                uint16_t port = 0;
                if (net::ParseAddress(server, 3478, name, port))
                {
                    auto found = m_resolve(name, port);
                    servers.insert(servers.end(), found.begin(), found.end());
                }
            }
            lock.lock();
            if (generation == m_generation)
                m_servers = std::move(servers);
        }
    }
}
