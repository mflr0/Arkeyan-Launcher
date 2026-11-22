#pragma once

#include "launcher/online/lobby.h"
#include "launcher/profiles.h"
#include "shared/collection/catalogue.h"
#include <filesystem>

namespace online
{
    enum class InputError
    {
        None,
        Picture,
        Figure,
        Save,
        SaveTooLarge
    };

    struct RoomInputs
    {
        Hello hello;
        Player player;
        Settings settings;
        std::vector<SaveFile> save;
        std::vector<std::string> localFigureFiles;
        InputError error = InputError::None;
    };

    // How PLAY starts this game (the game's Online tab, `<game>.online` in launcher.ini): alone, opening a room for a
    // friend, or joining the friend's room at `address`. Kept after a session, so the same friends play again in one press.
    enum class Mode
    {
        Solo,
        Host,
        Join
    };
    struct GameOnline
    {
        Mode mode = Mode::Solo;
        uint16_t port = DefaultPort;
        bool automatic = true; // ask the router to open the port (UPnP, NAT-PMP)
        std::string address;   // Join: the host's "IP:port"
        std::string password;
        bool crossPlatform = false; // this player allows a session with another system
    };
    GameOnline ReadGameOnline(const LauncherConfig& config, std::string_view game);
    // A game's version as shown: "-o" after it when the game is set to play online (the game shows the same in a
    // session); nothing for an unknown version.
    inline std::string VersionLabel(const std::string& version, bool online)
    {
        return version.empty() || !online ? version : version + "-o";
    }
    void WriteGameOnline(LauncherConfig& config, std::string_view game, const GameOnline& online);

    std::string RoomPlatform();
    Settings RoomSettings(const LauncherConfig& config, std::string_view game);
    RoomInputs ReadRoomInputs(const LauncherConfig& config, const profiles::Profile& profile,
        const collection::Game& game, const std::string& xexSha256, bool host, const std::string& seatsFile,
        const std::filesystem::path& saves, bool readSave = true);
}
