#pragma once

// One launcher per data folder (docs/launcher_ui.md, "One launcher at a time"). The first start holds an exclusive lock
// of the system on <data root>/launcher.lock for its whole life: the system releases it when the process ends, even in a
// crash, so a lock file left behind never blocks a start. A second start leaves a request for the first in
// <data root>/launcher-request.txt (written whole, then renamed into place) and exits; the first reads it in its loop and
// removes it. No UI here: ArkeyanLauncherTest runs these on scratch folders.
#include <cstdint>
#include <filesystem>
#include <string>

namespace single_instance
{
    std::filesystem::path LockFile(const std::filesystem::path& root);
    std::filesystem::path RequestFile(const std::filesystem::path& root);

    class Lock
    {
    public:
        Lock() = default;
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        ~Lock() { Release(); }
        // Takes the lock of `root` (made if needed), trying again for up to `waitMs` (the self-update: the old launcher is
        // still closing). On success this process's id is written in the file, for the next start to read, and a request
        // left by a launcher that ended before reading it is removed. False when another process holds it.
        bool Acquire(const std::filesystem::path& root, uint32_t waitMs = 0);
        void Release();
        bool Held() const;

    private:
#if defined(_WIN32)
        void* m_handle = nullptr;
#else
        int m_fd = -1;
#endif
    };

    // The process id written in the lock file by the launcher that holds it (0 when unknown).
    uint32_t HolderPid(const std::filesystem::path& root);

    // What a second start asks of the running launcher: come to the front, or that and play a game as its PLAY would.
    struct Request
    {
        enum class Kind { None, Focus, Play };
        Kind kind = Kind::None;
        std::string game; // Play: a game id of the game table (FindGame)
    };
    bool SendRequest(const std::filesystem::path& root, const Request& request);
    // The request waiting, removed once read. Anything else than "focus" or "play <known game id>" is removed and ignored.
    Request TakeRequest(const std::filesystem::path& root);

    // What a second start does when Acquire failed: lets the holder take the foreground (Windows), leaves `request`, and
    // waits up to `waitMs` for the holder to take it. True when the request was handed over (or is left for the holder:
    // it still holds the lock); false when the holder ended meanwhile and `lock` now holds the lock: this process runs.
    bool HandOver(Lock& lock, const std::filesystem::path& root, const Request& request, uint32_t waitMs = 1500);
}
