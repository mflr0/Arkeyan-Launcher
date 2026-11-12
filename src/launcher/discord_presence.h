#pragma once

// Discord Rich Presence while a game runs (docs/settings.md, `[discord]`): the game's icon and name, its total play time,
// the time of this session, its version and a button to the project's GitHub. Spoken straight to the Discord app over its
// local IPC (a named pipe on Windows, a Unix socket elsewhere): no Discord SDK, nothing leaves the computer but through
// Discord itself. A worker thread does the talking, so a Discord that is closed, slow or restarted never stalls the window;
// it connects again by itself.
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace discordpresence
{
    // The application on discord.com/developers. Its Rich Presence art assets: each game's logo named after the game's id
    // ("giants"), each character's picture "fig_<toy type>" ("fig_103").
    inline constexpr char ApplicationId[] = "1570426097903083671";
    inline constexpr char RepositoryUrl[] = "https://github.com/mflr0/Arkeyan-Launcher";
    // The launcher's icon: in the corner of the game's, alone in the launcher (an image URL needs no upload to the application).
    // The player's figure in the corner: the application's art asset "fig_<toy type>" (tools/discord_icons.cpp writes them
    // from the Toy Box's pictures, to upload on discord.com/developers).
    inline constexpr char FigureAssetPrefix[] = "fig_";
    inline constexpr char LauncherIconUrl[] = "https://raw.githubusercontent.com/mflr0/Arkeyan-Launcher/main/resources/icons/skylauncher-128.png";

    // What Discord shows; `shown` false: nothing.
    struct Activity
    {
        bool shown = false;
        std::string details;      // the first line: the game's name, or "In the launcher"
        std::string state;        // the second line: "12 h 34 in total · v0.8.2"
        bool namedByDetails = false; // the member list says "Playing <details>" instead of the application's name
        std::string largeImage;   // an art asset's key ("giants") or an image URL
        std::string largeText;    // its tooltip
        std::string smallImage;   // the corner's image; empty: none
        std::string smallText;
        int64_t start = 0;        // unix time: Discord counts the time from it; 0: no time
        bool operator==(const Activity&) const = default;
    };

    // The SET_ACTIVITY command's JSON for `activity` (null when it shows nothing), for process `pid`.
    std::string ActivityCommand(const Activity& activity, uint32_t pid, const std::string& nonce);

    class Client
    {
    public:
        Client() = default;
        ~Client(); // clears the activity and ends the worker
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        // What to show from now on; nothing is sent when it is the same as before. The worker starts at the first call.
        void Set(const Activity& activity);

    private:
        void Run();
        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::thread m_worker;
        Activity m_activity;
        uint64_t m_version = 0; // bumped at each change: the worker sends when it has not sent this one
        bool m_stop = false;
    };
}
