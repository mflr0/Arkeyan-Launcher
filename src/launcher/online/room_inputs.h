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

    std::string RoomPlatform();
    Settings RoomSettings(const LauncherConfig& config, std::string_view game);
    RoomInputs ReadRoomInputs(const LauncherConfig& config, const profiles::Profile& profile,
        const collection::Game& game, const std::string& xexSha256, bool host, const std::string& seatsFile,
        const std::filesystem::path& saves, bool readSave = true);
}
