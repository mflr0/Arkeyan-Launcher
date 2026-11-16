#pragma once

// Moving the games to another Games folder (Settings > Launcher; docs/install.md, "Games folder"): each game's built game,
// HD textures and copied game data go from <old>/<Game> to <new>/<Game> (the data folder's <Game> when either is the
// default). Nothing is rebuilt. Each entry is renamed when both places are on one volume, else copied, checked byte for
// byte, and only then deleted. A stop or a failure puts back what was renamed and deletes the copies made so far, so the
// old place keeps everything until the whole move is done. No UI: ArkeyanLauncherTest runs it on scratch folders.
#include "launcher/launcher.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gamesfolder
{
    // Why a folder can't be the games folder.
    enum class Refusal
    {
        None,
        Missing,  // not an existing folder
        ReadOnly, // nothing can be written in it
        InGame,   // inside a game's folder (in the data folder or in the games folder), or one of them
        InKits,   // inside the downloaded kits
    };
    // `folder` empty: the default, always fine.
    Refusal Check(const std::filesystem::path& folder);
    const char* RefusalText(Refusal refusal); // in the launcher's language

    struct Entry
    {
        std::string game; // its id
        std::filesystem::path from, to;
        uint64_t bytes = 0;
    };
    struct Plan
    {
        std::filesystem::path from, to; // the games folders; empty: the data folder
        std::vector<Entry> entries;
        uint64_t bytes = 0;
    };
    // What moves from the games folder `from` to `to` (either empty: the data folder): out of the data folder, the built
    // game (its runtime, icons, notices, fonts and file list) and its textures-hd/ and gamedata/, but those two stay for a
    // game built before the games folder (BuildKnowsGamesFolder); out of a games folder, everything in each game's folder.
    Plan MakePlan(const LauncherConfig& config, const std::filesystem::path& from, const std::filesystem::path& to);

    struct Options
    {
        bool forceCopy = false; // skip the rename, as if the folders were on two volumes (tests)
        int64_t freeBytes = -1; // the room at the target; -1: ask the system (tests set it)
        std::function<void(uint64_t done)> progress; // bytes moved so far, from the moving thread
    };
    struct Result
    {
        enum class State
        {
            Done,
            Stopped, // asked to stop: everything is back where it was
            Failed,  // a file couldn't be copied or put in place: what had moved went back (`error`)
            NoRoom,  // not enough free space at the target for what must be copied (`needed`): nothing was touched
        };
        State state = State::Done;
        uint64_t needed = 0;
        std::string error;
    };
    Result Move(const Plan& plan, const Options& options, const std::atomic<bool>& stop);
    // After a move: every path of the settings into a moved entry ([install.*] runtime=, a game's iso= when it is the
    // copied game data) points at its new place, and "[launcher] games=" is the new folder (removed for the default).
    void Record(LauncherConfig& config, const Plan& plan);

    // Move on a thread, for the window.
    class Mover
    {
    public:
        ~Mover();
        bool Start(Plan plan, Options options = {}); // false while one runs
        void Stop() { m_stop = true; }
        bool Running() const { return m_running; }
        uint64_t Done() const { return m_done; }
        uint64_t Total() const { return m_total; }
        // Once it has ended: the plan and how it went (taken once; false while it runs or when there is nothing new).
        bool TakeResult(Plan& plan, Result& result);

    private:
        std::thread m_thread;
        std::atomic<bool> m_running{ false }, m_stop{ false }, m_ended{ false };
        std::atomic<uint64_t> m_done{ 0 }, m_total{ 0 };
        std::mutex m_mutex;
        Plan m_plan;
        Result m_result;
    };
}
