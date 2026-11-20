#pragma once

// From a started room to the game (docs/online.md, "Starting"): both launchers start the game from a session folder of
// the same content (the host's save, every figure of both players, portal.txt with the host's figures on player 1's
// seats and the guest's on player 2's, both pictures), so both games start identical. After the game, the host's save goes
// back to the host's own saves and each player's figures back to that player's own collection files.
#include "launcher/game_run.h"
#include "launcher/online/lobby.h"
#include "shared/collection/catalogue.h"
#include <filesystem>
#include <string>
#include <vector>

namespace online
{
    struct SessionPlan
    {
        bool host = false;
        std::string hostAddress; // guest: the host as the room reached it (no port)
        Start start;
        Player hostPlayer, guestPlayer; // Player 1 and Player 2 in both games
        Settings settings;              // the host's
        std::vector<SaveFile> save;     // the host's save
        // The local player's collection files, one per figure of its Player (same order), for the write-back.
        std::vector<std::string> localFigureFiles;
    };

    // The seats of `game` both games start with: one figure file name per seat ("" free), relative to the figures
    // folder. The host's figures keep their seats; the guest's figure on its own n-th player-1 seat goes to the n-th
    // player-2 seat; shared seats (item, adventure, trap) keep the host's figures only.
    std::vector<std::string> SessionSeats(
        const collection::Game& game, const Player& hostPlayer, const Player& guestPlayer);

    // Writes the session folder `root` (emptied first) and adds the online variables to `launch` (replacing any of
    // the same name). False with `error` when a file cannot be written.
    bool PrepareSession(const collection::Game& game, const SessionPlan& plan, const std::filesystem::path& root,
        GameLaunch& launch, std::string& error);

    // After the game exits: the host's save files back into `realSaves` (each replaced file first copied to
    // `realSaves`-backup), the local player's figures back to their collection files. Nothing when `root` holds no
    // finished session. Safe to call again (a launcher closed during the game finishes it at its next start).
    bool FinishSession(const std::filesystem::path& root, const std::filesystem::path& realSaves, std::string& error);

    // The game's "Play online" (its F3 menu) quits and leaves open-online.txt, the game's id, in its saves `saves`: the
    // id (the file is removed), else "".
    std::string TakeOpenOnline(const std::filesystem::path& saves);
}
