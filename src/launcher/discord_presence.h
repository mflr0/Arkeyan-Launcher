#pragma once

// Discord Rich Presence while a game runs (docs/settings.md, `[discord]`): the game's icon and name, its total play time,
// the time of this session, its version and a button to the project's GitHub. Spoken straight to the Discord app over its
// local IPC (a named pipe on Windows, a Unix socket elsewhere): no Discord SDK, nothing leaves the computer but through
// Discord itself. A worker thread does the talking, so a Discord that is closed, slow or restarted never stalls the window;
// it connects again by itself.
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace discordpresence
{
    // The application on discord.com/developers. Its art assets: each game's logo on a square, named after the game's id
    // ("giants"), uploaded once (Discord crops a wide picture to a square); every other picture is an address.
    inline constexpr char ApplicationId[] = "1570426097903083671";
    inline constexpr char RepositoryUrl[] = "https://github.com/mflr0/Arkeyan-Launcher";
    inline constexpr char DiscordUrl[] = "https://discord.gg/UMQY4SAS3F";
    // The launcher's icon (this repository's PNG of the README's logo), alone in the launcher and in place of a missing
    // logo or picture.
    inline constexpr char LauncherIconUrl[] =
        "https://raw.githubusercontent.com/mflr0/Arkeyan-Launcher/main/resources/icons/arkeyan-launcher-128.png";
    // The game's logo and a character's picture, as image addresses (discord_images.cpp); empty when there is none, and
    // the launcher's icon stands in.
    std::string GameLogoUrl(std::string_view gameId);
    std::string FigureImageUrl(uint16_t toyType);

    // What the running game wrote in <its player folder>/presence.txt: level, menu, players, and player 1's toy type,
    // variant id and optional toylevel as unsigned decimal numbers. Toy fields are absent when no character is present;
    // `known` is false when the file is missing (an older game).
    struct GameStatus
    {
        bool known = false;
        std::string level; // the runtime's level id ("level_213_tutotial", "academy/academy")
        bool menu = false; // the title or the front end
        int players = 0;
        std::optional<uint16_t> toy, variant;
        std::optional<uint32_t> toyLevel;
    };
    GameStatus ParseGameStatus(std::string_view text);

    // A figure by its toy type and variant id, and its level when known.
    struct Toy
    {
        uint16_t type = 0, variant = 0;
        std::optional<uint32_t> level;
    };

    // Player 1 as the running game plays it, against the figure the launcher's seats (portal.txt) put on player 1. With a
    // real portal plugged in, the game neither reads nor writes portal.txt, so those seats are stale: a figure that differs
    // on two reads in a row (one may fall while the game writes portal.txt) means a real portal, until Reset (the game ends).
    class PlayerOneWatch
    {
    public:
        void Reset() { *this = {}; }
        // `status`: the game's presence.txt; `seatOne`: the figure on player 1's seat in portal.txt, none when empty.
        void Update(const GameStatus& status, const std::optional<Toy>& seatOne);
        // The figure the game plays as player 1; none when the game reports none (empty, or a game from before 0.9.7).
        const std::optional<Toy>& PlayerOne() const { return m_playerOne; }
        bool RealPortal() const { return m_realPortal; } // the seats of portal.txt are not what the game plays
        // Only the seats tell the figure: the game names none and no real portal was seen.
        bool UseSeats() const { return !m_playerOne && !m_realPortal; }

    private:
        std::optional<Toy> m_playerOne;
        bool m_realPortal = false;
        bool m_differed = false; // the last read differed
    };

    // The name the game shows for `level`, from its levels.json (docs/level_names.md: "levels": {"<level id>":
    // {"<language>": "<name>"}}), in `language`, else English, else any; empty when unknown.
    std::string LevelName(std::string_view levelsJson, std::string_view level, std::string_view language);

    // What Discord shows; `shown` false: nothing.
    struct Activity
    {
        bool shown = false;
        std::string details;      // the first line: the game's name, or "In the launcher"
        std::string state;        // the second line: "12 h 34 in total · v0.8.2"
        bool namedByDetails = false; // the member list says "Playing <details>" instead of the application's name
        std::string largeImage;   // an image's https address (or an art asset's key)
        std::string largeText;    // its tooltip
        std::string smallImage;   // the corner's image; empty: none
        std::string smallText;
        int64_t start = 0;        // unix time: Discord counts the time from it; 0: no time
        int partySize = 0, partyMax = 0; // "2 of 2" players; 0: none shown
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
