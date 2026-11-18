#pragma once

// Imaginite chests added from the launcher (docs/launcher_ui.md, "Imaginite chests"). The game writes its pile in the player
// folder (chests.txt) each time a game reaches a zone; the launcher asks for more in chests_add.txt, which the game claims
// the next time that save slot enters the Academy (shared/collection/chests.h).
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

struct ChestKindRow
{
    std::string_view key; // its name in the two files
    const char* label;    // English, translated by Tr
};

// The games whose chest pile the launcher can add to (Imaginators).
bool ChestsOffered(std::string_view gameId);
// The kinds the page offers, in its order (empty for another game).
std::span<const ChestKindRow> ChestKinds(std::string_view gameId);

struct GameChests
{
    bool known = false; // the game read a slot's pile at least once
    int slot = -1;      // the save slot played last, from 0
    int pile = 0;       // the chests in its pile, not opened yet
    int waiting = 0;    // the chests asked for and not added yet
    int waitingSlot = -1;
    std::vector<int> waitingByKind;
    int room = 0; // how many more can be asked for
    // Waiting chests are for another slot than the one played last: no more can be asked for until they are added or
    // cancelled.
    bool WaitingElsewhere() const { return waiting > 0 && waitingSlot != slot; }
};

// The pile and what waits, from the player folder of `gameId`.
GameChests ReadGameChests(const std::filesystem::path& folder, std::string_view gameId);
// Adds `counts` (one per kind) to the chests waiting for the slot played last. False, and nothing written, when the pile
// is not known, the chests do not fit in the room left, none are asked for, or chests wait for another slot.
bool AddChests(const std::filesystem::path& folder, std::string_view gameId, const std::vector<int>& counts);
// Removes the waiting chests. False when there were none, or the game already claimed them.
bool CancelChests(const std::filesystem::path& folder);
