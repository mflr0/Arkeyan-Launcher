#pragma once
#include <filesystem>

// The launcher's own messages (SDL_Log: the room's [online] lines, the updater's) in <logs>/launcher.log, the run before
// in launcher.prev.log (docs/support.md). A Windows app has no console: without the file, a player could not send them.
namespace launcherlog
{
    void Start(const std::filesystem::path& folder);
    // Back to SDL's own output, the file closed (tests).
    void Stop();
}
