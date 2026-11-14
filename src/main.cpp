// Arkeyan Launcher: shows the launcher; PLAY starts the game runtime from the window, which stays open (game_run.h, GameSession).
#include "shared/collection/datadir.h"
#include "launcher/game_run.h"
#include "launcher/game_settings.h"
#include "launcher/launcher.h"
#include "launcher/problem_report.h"
#include "launcher/shortcut.h"
#include "launcher/single_instance.h"
#include "launcher/ui/launcher_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    LauncherArguments arguments = ParseLauncherArguments(argc, argv);
    if (arguments.help)
    {
        fprintf(stderr, "%s\n", LauncherUsage());
#ifdef _WIN32
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_INFORMATION, "Arkeyan Launcher", LauncherUsage(), nullptr); // a Windows app has no console
#endif
        return 0;
    }
    for (const std::string& unknown : arguments.unknown)
        fprintf(stderr, "launcher: ignoring the option %s (--help lists them)\n", unknown.c_str());
    datadir::ApplyPortable(); // portable.txt beside the launcher: its data folder, the games' too
    // The Toy Box window: the launcher started again by itself for the game running, beside the one launcher (it holds no
    // lock, starts no game and checks no update; docs/toybox_window.md).
    if (!arguments.toybox.empty())
    {
        const GameInfo* toyboxGame = FindGame(arguments.toybox);
        if (toyboxGame == nullptr)
        {
            fprintf(stderr, "launcher: --toybox: no game called \"%s\"\n", arguments.toybox.c_str());
            return 1;
        }
        std::string toyboxConfigPath = LauncherConfigPath();
        LauncherConfig toyboxConfig = LoadLauncherConfig(toyboxConfigPath);
        LauncherOptions toyboxOptions;
        toyboxOptions.toybox = toyboxGame->id;
        RunLauncherWindow(*toyboxGame, toyboxConfigPath, toyboxConfig, {}, nullptr, nullptr, toyboxOptions);
        SDL_Quit();
        return 0;
    }
    // One launcher per data folder, decided before any window, update check or file write: a second start hands its
    // request (come to the front; a shortcut's game to play) to the one running and exits (single_instance.h).
    single_instance::Lock instance;
    if (!instance.Acquire(datadir::Root(), arguments.afterUpdate ? 10000 : 0))
    {
        single_instance::Request request;
        request.kind = single_instance::Request::Kind::Focus;
        if (arguments.play && FindGame(arguments.game) != nullptr)
            request = { single_instance::Request::Kind::Play, arguments.game };
        if (single_instance::HandOver(instance, datadir::Root(), request))
        {
            fprintf(stderr, "launcher: already running for %s: handed it the request\n",
                datadir::Text(datadir::Root()).c_str());
            return 0;
        }
    }
    datadir::MigrateOnce(); // the older folders into <data root> (docs/install.md), before any path is used
    std::string configPath = LauncherConfigPath();
    LauncherConfig config = LoadLauncherConfig(configPath);
    MigrateFrameRates(config);

    // Shortcuts made by a launcher called Skylauncher start this one and carry its name.
    {
        std::vector<std::pair<std::string, std::string>> shortcutGames;
        for (const GameInfo& info : g_games)
            if (FindEnabledGame(info.id, config) != nullptr)
                shortcutGames.emplace_back(info.id, info.name);
        RefreshShortcuts(DesktopFolder(), shortcutGames, SelfExecutable());
    }

    // The game last shown ([launcher] game=), else Spyro's Adventure. Picking another game in the sidebar switches the
    // window to it in place; the game that was shown when PLAY was pressed is the one that runs.
    const GameInfo* game = arguments.game.empty() ? FindEnabledGame(config["launcher"]["game"], config)
                                                  : FindEnabledGame(arguments.game, config);
    if (!arguments.game.empty() && game == nullptr)
        fprintf(stderr, "launcher: no game called \"%s\" (see --help)\n", arguments.game.c_str());
    if (game == nullptr)
        game = FindEnabledGame(config["launcher"]["game"], config);
    if (game == nullptr)
        game = FindGame("ssa");
    if (game == nullptr)
        return 1;
    std::string installed;
    LauncherOptions options;
    options.openGame = !arguments.game.empty() && FindEnabledGame(arguments.game, config) != nullptr;
    options.play = arguments.play && (arguments.game.empty() || FindEnabledGame(arguments.game, config) != nullptr);
    options.startGames = true;
    options.requests = true;
    std::string chosen =
        RunLauncherWindow(*game, configPath, config, config[game->id]["iso"], &installed, &game, options);
    if (chosen.empty())
        return 0;

    // A screenshot script that plays (SKY_LAUNCHER_TEST_PLAY) returns its choice: the game runs here, the window closed. PLAY
    // itself starts the game from the window, which stays (GameSession).
    GameLaunch launch = PrepareGameLaunch(*game, config, configPath, installed);
    if (launch.runtime.empty())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game->name, launch.error.c_str(), nullptr);
        return 1;
    }
    for (const auto& [name, value] : launch.variables)
        SetGameVariable(name.c_str(), value.c_str());
    GameRunResult run = RunGame(launch.runtime, chosen);
    // The game's own menu (F3) writes [<game>.video] and [<game>.audio] itself, and they win over [video]: nothing is copied back.
    if (!run.started)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, game->name, run.error.c_str(), nullptr);
        return 1;
    }
    if (run.exitCode != 0)
        ShowGameStopped(*game, run.exitCode, configPath, LoadLauncherConfig(configPath), chosen);
    SDL_Quit();
    return 0;
}
