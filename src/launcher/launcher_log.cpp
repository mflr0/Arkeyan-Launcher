#include "launcher/launcher_log.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace launcherlog
{
    namespace
    {
        std::mutex g_mutex;
        FILE* g_file = nullptr;
        SDL_LogOutputFunction g_previous = nullptr;
        void* g_previousData = nullptr;

        void Output(void*, int category, SDL_LogPriority priority, const char* message)
        {
            {
                std::lock_guard lock(g_mutex);
                if (g_file != nullptr)
                {
                    const std::time_t now = std::time(nullptr);
                    std::tm local{};
#if defined(_WIN32)
                    localtime_s(&local, &now);
#else
                    localtime_r(&now, &local);
#endif
                    char stamp[16];
                    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &local);
                    std::fprintf(g_file, "%s %s\n", stamp, message);
                    std::fflush(g_file); // a crash or a killed launcher keeps every line before it
                }
            }
            if (g_previous != nullptr)
                g_previous(g_previousData, category, priority, message);
        }
    }

    void Start(const std::filesystem::path& folder)
    {
        std::lock_guard lock(g_mutex);
        // Closed before the rename: Windows does not rename a file that is open.
        if (g_file != nullptr)
            std::fclose(g_file);
        g_file = nullptr;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::filesystem::rename(folder / "launcher.log", folder / "launcher.prev.log", error);
#if defined(_WIN32)
        g_file = _wfopen((folder / "launcher.log").c_str(), L"w");
#else
        g_file = std::fopen((folder / "launcher.log").c_str(), "w");
#endif
        if (g_previous == nullptr)
            SDL_GetLogOutputFunction(&g_previous, &g_previousData);
        SDL_SetLogOutputFunction(Output, nullptr);
    }

    void Stop()
    {
        std::lock_guard lock(g_mutex);
        if (g_previous != nullptr)
            SDL_SetLogOutputFunction(g_previous, g_previousData);
        g_previous = nullptr;
        g_previousData = nullptr;
        if (g_file != nullptr)
            std::fclose(g_file);
        g_file = nullptr;
    }
}
