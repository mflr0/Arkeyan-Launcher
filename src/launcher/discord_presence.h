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
#include <string_view>
#include <thread>

namespace discordpresence
{
    // The application on discord.com/developers. Its one art asset: the launcher's icon, "launcher".
    inline constexpr char ApplicationId[] = "1570426097903083671";
    inline constexpr char RepositoryUrl[] = "https://github.com/mflr0/Arkeyan-Launcher";
    // The launcher's icon (the art asset), alone in the launcher and in the corner when no figure has a picture.
    inline constexpr char LauncherIconAsset[] = "launcher";
    // The game's logo and a character's picture, as image addresses Discord fetches (discord_images.cpp); empty when
    // there is none, and the launcher's icon stands in.
    std::string GameLogoUrl(std::string_view gameId);
    std::string FigureImageUrl(uint16_t toyType);

    // What Discord shows; `shown` false: nothing.
    struct Activity
    {
        bool shown = false;
        std::string details;      // the first line: the game's name, or "In the launcher"
        std::string state;        // the second line: "12 h 34 in total · v0.8.2"
        bool namedByDetails = false; // the member list says "Playing <details>" instead of the application's name
        std::string largeImage;   // an art asset's key ("launcher") or an image's https address
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
