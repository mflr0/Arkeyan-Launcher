#pragma once

// The Toy Box window's end of the link to the running game (docs/toybox_window.md; the messages: src/shared/toybox_link.h).
// The game writes its port and a token in toybox.link beside its portal.txt while it runs; this connects to it on the
// loopback, keeps the game's seats as it last sent them and sends the window's commands. A thread does the waiting, so the
// window never blocks on the game. No UI here: ArkeyanLauncherTest runs it against a fake game.
#include "shared/toybox_link.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace toyboxlinkclient
{
    // Where `sessionFile` (the game's portal.txt, collection_model.h SessionFile) has its link file.
    std::string LinkFileBeside(const std::string& sessionFile);

    class Client
    {
    public:
        enum class State
        {
            Waiting,      // no game answers yet (starting, or not running)
            Connected,    // the seats are the game's
            OtherVersion, // the game speaks another version of the link: one of the two needs updating
            Busy,         // another Toy Box window has the game (tried again every few seconds)
            Ended         // the game said bye: it is closing
        };
        // A command's answer: "put", "off", "swap" or "clear", the seat (-1: none), "ok" or a toyboxlink::refusal.
        struct Done
        {
            std::string verb;
            int seat = -1;
            std::string result;
        };

        explicit Client(std::string linkFile, std::string clientName = "launcher");
        ~Client();
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        State Current() const { return m_state.load(); }
        int GameVersion() const { return m_gameVersion.load(); } // OtherVersion: the game's link version
        std::string Game() const;                               // the game's id once connected
        // Bumps every time the game sent its seats: read Seats() again.
        uint64_t Generation() const { return m_generation.load(); }
        std::vector<toyboxlink::Seat> Seats() const;
        // Commands; false when not connected (nothing is sent).
        bool Put(int seat, const std::string& path);
        bool Off(int seat);
        bool Swap();
        bool Clear();
        bool NextDone(Done& done);

    private:
        void Run();
        bool Send(const toyboxlink::Message& message);

        std::string m_file, m_name;
        std::atomic<State> m_state{ State::Waiting };
        std::atomic<int> m_gameVersion{ 0 };
        std::atomic<uint64_t> m_generation{ 0 };
        std::atomic<bool> m_stop{ false };
        mutable std::mutex m_mutex;
        std::string m_game;
        std::vector<toyboxlink::Seat> m_seats;
        std::string m_outgoing;
        std::deque<Done> m_done;
        std::thread m_thread;
    };
}
