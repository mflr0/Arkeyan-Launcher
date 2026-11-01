#pragma once

#include <SDL3/SDL_timer.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace launcher_files
{
    // Each writer exclusively creates its own sibling file. A stale temporary, a link or another launcher's write is
    // never opened or removed. Flush the bytes before the atomic replacement so a failed write keeps the old file.
    inline bool WriteAtomic(const std::filesystem::path& target, std::string_view text)
    {
        namespace fs = std::filesystem;
        if (target.empty() || !target.has_filename())
            return false;
        std::error_code error;
        if (!target.parent_path().empty())
            fs::create_directories(target.parent_path(), error);
        if (error)
            return false;
        static std::atomic<unsigned long long> serial{ 0 };
#if defined(_WIN32)
        const auto pid = _getpid();
#else
        const auto pid = getpid();
#endif
        fs::path temporary;
        int fd = -1;
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            temporary = target;
            temporary += ".tmp." + std::to_string(pid) + "." + std::to_string(serial++);
#if defined(_WIN32)
            fd = _wopen(temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
            fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
            if (fd >= 0 || errno != EEXIST)
                break;
        }
        if (fd < 0)
            return false;
#if defined(_WIN32)
        FILE* file = _fdopen(fd, "wb");
#else
        FILE* file = fdopen(fd, "wb");
#endif
        bool ok = false;
        if (file != nullptr)
        {
            ok = std::fwrite(text.data(), 1, text.size(), file) == text.size() && std::fflush(file) == 0;
#if defined(_WIN32)
            if (ok) ok = _commit(fd) == 0;
#else
            if (ok) ok = fsync(fd) == 0;
#endif
            if (std::fclose(file) != 0) ok = false;
        }
        else
        {
#if defined(_WIN32)
            _close(fd);
#else
            close(fd);
#endif
        }
        if (ok)
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                fs::rename(temporary, target, error);
                if (!error)
                {
#if !defined(_WIN32)
                    const fs::path parent = target.parent_path().empty() ? fs::path(".") : target.parent_path();
                    const int directory = open(parent.c_str(), O_RDONLY | O_CLOEXEC);
                    if (directory >= 0) { fsync(directory); close(directory); }
#endif
                    return true;
                }
                SDL_Delay(20);
            }
        fs::remove(temporary, error);
        return false;
    }
}
