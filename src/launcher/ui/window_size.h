// The Picture tab's Window size row (game_settings_page.cpp): which saved sizes show as Custom.
#pragma once
#include <string>
#include <string_view>

namespace launcher_ui
{
    // Custom picked by the player: it belongs to one game, since a preset value of another game must not show as Custom.
    struct WindowSizeCustom
    {
        std::string game;
        bool chosen = false;
    };

    // True for a size the row lists itself (1280x720, 1600x900, 1920x1080, 2560x1440).
    bool IsPresetWindowSize(std::string_view value);

    // Whether `game`'s saved window_size shows as Custom: a size that matches no preset, or Custom picked for this game.
    // On another game than the last one, the choice is dropped and worked out again from that game's value.
    bool ShowsCustomWindowSize(WindowSizeCustom& state, std::string_view game, std::string_view value);
}
