#include "launcher/single_instance.h"
#include "launcher/atomic_file.h"
#include "launcher/launcher.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace single_instance
{
    namespace fs = std::filesystem;

    namespace
    {
        uint32_t ThisPid()
        {
#if defined(_WIN32)
            return uint32_t(GetCurrentProcessId());
#else
            return uint32_t(getpid());
#endif
        }

        void Pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
    }

    fs::path LockFile(const fs::path& root) { return root / "launcher.lock"; }
    fs::path RequestFile(const fs::path& root) { return root / "launcher-request.txt"; }

    bool Lock::Acquire(const fs::path& root, uint32_t waitMs)
    {
        Release();
        std::error_code error;
        fs::create_directories(root, error);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
        const std::string pid = std::to_string(ThisPid()) + "\n";
        for (;;)
        {
#if defined(_WIN32)
            // Shared for reading, so the next start reads the id; for deleting, so "Uninstall everything" can remove the
            // folder. The byte locked lies far past the text: reading the id is never blocked by the lock.
            HANDLE handle = CreateFileW(LockFile(root).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE)
            {
                OVERLAPPED at{};
                at.OffsetHigh = 1;
                if (LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at))
                {
                    DWORD written = 0;
                    SetFilePointer(handle, 0, nullptr, FILE_BEGIN);
                    SetEndOfFile(handle);
                    WriteFile(handle, pid.data(), DWORD(pid.size()), &written, nullptr);
                    m_handle = handle;
                }
                else
                    CloseHandle(handle);
            }
#else
            // Not inherited by the games the launcher starts: a game left running never holds the launcher's lock.
            int fd = open(LockFile(root).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
            if (fd >= 0)
            {
                if (flock(fd, LOCK_EX | LOCK_NB) == 0)
                {
                    if (ftruncate(fd, 0) != 0 || pwrite(fd, pid.data(), pid.size(), 0) != ssize_t(pid.size()))
                        fprintf(stderr, "launcher: couldn't write the process id in %s\n", LockFile(root).string().c_str());
                    m_fd = fd;
                }
                else
                    close(fd);
            }
#endif
            if (Held())
            {
                fs::remove(RequestFile(root), error); // left by a launcher that ended before reading it
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            Pause(100);
        }
    }

    void Lock::Release()
    {
#if defined(_WIN32)
        if (m_handle != nullptr)
            CloseHandle(m_handle);
        m_handle = nullptr;
#else
        if (m_fd >= 0)
            close(m_fd);
        m_fd = -1;
#endif
    }

    bool Lock::Held() const
    {
#if defined(_WIN32)
        return m_handle != nullptr;
#else
        return m_fd >= 0;
#endif
    }

    uint32_t HolderPid(const fs::path& root)
    {
        std::ifstream file(LockFile(root));
        unsigned long pid = 0;
        if (!(file >> pid) || pid > UINT32_MAX)
            return 0;
        return uint32_t(pid);
    }

    bool SendRequest(const fs::path& root, const Request& request)
    {
        if (request.kind == Request::Kind::Play)
            return FindGame(request.game) != nullptr && launcher_files::WriteAtomic(RequestFile(root), "play " + request.game + "\n");
        return request.kind == Request::Kind::Focus && launcher_files::WriteAtomic(RequestFile(root), "focus\n");
    }

    Request TakeRequest(const fs::path& root)
    {
        // Moved aside first: a request written while this one is read is the next one, not lost.
        std::error_code error;
        fs::path taken = RequestFile(root);
        taken += ".taken." + std::to_string(ThisPid());
        fs::rename(RequestFile(root), taken, error);
        if (error)
            return {};
        char buffer[64] = {};
        std::ifstream file(taken, std::ios::binary);
        file.read(buffer, sizeof(buffer) - 1);
        file.close();
        fs::remove(taken, error);
        std::string_view text(buffer, size_t(file.gcount()));
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
            text.remove_suffix(1);
        Request request;
        if (text == "focus")
            request.kind = Request::Kind::Focus;
        else if (text.starts_with("play ") && FindGame(text.substr(5)) != nullptr)
        {
            request.kind = Request::Kind::Play;
            request.game = std::string(text.substr(5));
        }
        return request;
    }

    bool HandOver(Lock& lock, const fs::path& root, const Request& request, uint32_t waitMs)
    {
#if defined(_WIN32)
        // Windows gives the foreground only to a process the one that has it allows: this one, started by the player.
        if (uint32_t pid = HolderPid(root); pid != 0)
            AllowSetForegroundWindow(DWORD(pid));
#endif
        if (!SendRequest(root, request))
            return true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::error_code error;
            if (!fs::exists(RequestFile(root), error))
                return true;
            if (lock.Acquire(root)) // the holder ended before reading it (Acquire removed the request)
                return false;
            Pause(50);
        }
        return true;
    }
}
