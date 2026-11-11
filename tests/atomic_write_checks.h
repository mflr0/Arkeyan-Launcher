#pragma once

#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <sys/resource.h>
#endif

// Fail a real write without depending on the writer's choice of temporary name. Keep reads available so callers can
// still validate figures and the old file. Restore the process state before the caller reports any failed checks.
template <class Action>
bool WithBlockedFileWrite(const std::filesystem::path& target, Action action)
{
#if defined(_WIN32)
    // Sharing reads but not deletion prevents atomic replacement of the existing target.
    HANDLE file = CreateFileW(
        target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    action();
    return CloseHandle(file) != 0;
#else
    (void)target;
    rlimit previous{};
    if (getrlimit(RLIMIT_FSIZE, &previous) != 0)
        return false;
    const auto signal = std::signal(SIGXFSZ, SIG_IGN);
    if (signal == SIG_ERR)
        return false;
    rlimit blocked = previous;
    blocked.rlim_cur = 0;
    if (setrlimit(RLIMIT_FSIZE, &blocked) != 0)
    {
        std::signal(SIGXFSZ, signal);
        return false;
    }
    action();
    const bool restored = setrlimit(RLIMIT_FSIZE, &previous) == 0;
    return std::signal(SIGXFSZ, signal) != SIG_ERR && restored;
#endif
}

inline bool NoAtomicTemporary(const std::filesystem::path& target)
{
    const auto prefix = target.filename().native() + std::filesystem::path(".tmp.").native();
    for (const auto& entry : std::filesystem::directory_iterator(target.parent_path()))
        if (entry.path().filename().native().starts_with(prefix))
            return false;
    return true;
}
