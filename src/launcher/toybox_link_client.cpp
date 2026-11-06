// The Toy Box window's end of the link (toybox_link_client.h).
#include "launcher/toybox_link_client.h"
#include "shared/toybox_socket.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace toyboxlinkclient
{
    std::string LinkFileBeside(const std::string& sessionFile)
    {
        if (sessionFile.empty())
            return {};
        return (std::filesystem::path(sessionFile).parent_path() / toyboxlink::FileName).string();
    }

    Client::Client(std::string linkFile, std::string clientName)
        : m_file(std::move(linkFile)), m_name(std::move(clientName))
    {
        m_thread = std::thread([this] { Run(); });
    }

    Client::~Client()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    std::string Client::Game() const
    {
        std::lock_guard lock(m_mutex);
        return m_game;
    }

    std::vector<toyboxlink::Seat> Client::Seats() const
    {
        std::lock_guard lock(m_mutex);
        return m_seats;
    }

    bool Client::Send(const toyboxlink::Message& message)
    {
        if (m_state != State::Connected)
            return false;
        std::string line = toyboxlink::Format(message);
        if (line.empty())
            return false;
        std::lock_guard lock(m_mutex);
        m_outgoing += line;
        return true;
    }

    bool Client::Put(int seat, const std::string& path) { return Send({ "put", { std::to_string(seat), path } }); }
    bool Client::Off(int seat) { return Send({ "off", { std::to_string(seat) } }); }
    bool Client::Swap() { return Send({ "swap", {} }); }
    bool Client::Clear() { return Send({ "clear", {} }); }

    bool Client::NextDone(Done& done)
    {
        std::lock_guard lock(m_mutex);
        if (m_done.empty())
            return false;
        done = std::move(m_done.front());
        m_done.pop_front();
        return true;
    }

    void Client::Run()
    {
        using Clock = std::chrono::steady_clock;
        if (!toyboxsocket::Startup())
            return;
        Clock::time_point nextTry{};
        while (!m_stop && m_state != State::Ended && m_state != State::OtherVersion)
        {
            if (Clock::now() < nextTry)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            nextTry = Clock::now() + std::chrono::milliseconds(500);
            // The game's link file: its port and token. None yet: the game is still starting, or not running.
            size_t size = 0;
            char* text = static_cast<char*>(SDL_LoadFile(m_file.c_str(), &size));
            toyboxlink::Endpoint endpoint;
            bool parsed = text != nullptr && toyboxlink::ParseEndpoint(std::string_view(text, size), endpoint);
            SDL_free(text);
            if (!parsed)
                continue;
            toyboxsocket::Handle handle = toyboxsocket::Connect(endpoint.port, 300);
            if (handle == toyboxsocket::Invalid)
                continue; // left by a game that ended without removing it, or not listening yet
            // The window speaks its version first; the game answers hello, or refused with what it speaks.
            toyboxsocket::Send(handle,
                toyboxlink::Format({ "hello", { std::to_string(toyboxlink::Version), endpoint.token, m_name } }));
            toyboxlink::LineBuffer buffer;
            std::vector<toyboxlink::Seat> pending;
            bool greeted = false;
            Clock::time_point helloBy = Clock::now() + std::chrono::seconds(3);
            char data[4096];
            while (!m_stop)
            {
                if (!greeted && Clock::now() > helloBy)
                    break;
                {
                    std::string out;
                    {
                        std::lock_guard lock(m_mutex);
                        out.swap(m_outgoing);
                    }
                    if (!out.empty() && !toyboxsocket::Send(handle, out))
                        break;
                }
                if (toyboxsocket::Wait(&handle, 1, 25) == 0)
                    continue;
                long got = toyboxsocket::Receive(handle, data, sizeof(data));
                if (got <= 0)
                    break;
                buffer.Append(data, size_t(got));
                std::string line;
                bool leave = false;
                while (!leave && buffer.Next(line))
                {
                    toyboxlink::Message message;
                    if (!toyboxlink::Parse(line, message))
                        continue;
                    const std::vector<std::string>& f = message.fields;
                    if (message.verb == "refused")
                    {
                        if (!f.empty() && f[0] == "version")
                        {
                            m_gameVersion = f.size() > 1 ? atoi(f[1].c_str()) : 0;
                            m_state = State::OtherVersion;
                        }
                        else if (!f.empty() && f[0] == "busy")
                        {
                            m_state = State::Busy;
                            nextTry = Clock::now() + std::chrono::seconds(3);
                        }
                        leave = true;
                    }
                    else if (message.verb == "hello")
                    {
                        int version = f.empty() ? 0 : atoi(f[0].c_str());
                        if (version != toyboxlink::Version)
                        {
                            m_gameVersion = version;
                            m_state = State::OtherVersion;
                            leave = true;
                            continue;
                        }
                        greeted = true;
                        {
                            std::lock_guard lock(m_mutex);
                            m_game = f.size() > 1 ? f[1] : std::string();
                        }
                        m_state = State::Connected;
                    }
                    else if (message.verb == "seat")
                    {
                        int index = -1;
                        toyboxlink::Seat seat;
                        if (toyboxlink::ParseSeat(message, index, seat))
                        {
                            if (size_t(index) >= pending.size())
                                pending.resize(size_t(index) + 1);
                            pending[size_t(index)] = seat;
                        }
                    }
                    else if (message.verb == "sync")
                    {
                        size_t count = f.empty() ? pending.size() : size_t(std::max(0, atoi(f[0].c_str())));
                        pending.resize(count);
                        {
                            std::lock_guard lock(m_mutex);
                            m_seats = pending;
                        }
                        m_generation++;
                    }
                    else if (message.verb == "done" && f.size() >= 3)
                    {
                        std::lock_guard lock(m_mutex);
                        m_done.push_back({ f[0], atoi(f[1].c_str()), f[2] });
                    }
                    else if (message.verb == "bye")
                    {
                        m_state = State::Ended;
                        leave = true;
                    }
                }
                if (leave || buffer.Overflow())
                    break;
            }
            toyboxsocket::Close(handle);
            if (m_state == State::Connected)
                m_state = State::Waiting; // the game went without a word (a crash): wait for it to come back
            {
                std::lock_guard lock(m_mutex);
                m_outgoing.clear();
            }
        }
    }
}
