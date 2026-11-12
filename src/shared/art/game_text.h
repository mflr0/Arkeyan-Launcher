#pragma once

// Each game's localized text, read from the player's disc: the upgrades of every Skylander (name,
// description, price, path, its bit in the figure's upgrade field) in English and French, written to
// <data>/<Game>/upgrades.json for collection::Upgrades (collection/upgrades.h); Imaginators' creation parts' names
// in the disc's languages, written to <data>/<Game>/parts.json for collection::PartName. The six games' localized level
// names go to <data>/<Game>/levels.json for presence. Game text: never shipped.
// The manifest (games/<id>/art.json) describes the sources in "upgrades", "parts" and "levels". Formats:
// docs/runtime/host/figures.md ("Upgrades", "The parts' names") and docs/level_names.md.
#include "game_art.h"
#include "../collection/json.h"
#include <filesystem>
#include <string_view>

namespace art
{
    constexpr int LevelNamesVersion = 1;

    // Runtime level id -> { language code: display name }. Reads only; unresolved names have no entry.
    collection::Json ReadLevelNamesFrom(const collection::Json& manifest, const Disc& disc);
    collection::Json ReadLevelNames(std::string_view game, const Disc& disc);

    // Writes the versioned levels.json cache atomically, including an empty mapping when no names are available.
    bool ExtractLevelNames(
        std::string_view game, const collection::Json& manifest, const Disc& disc, const std::filesystem::path& out);

    // Reads the upgrades of `game` as its manifest's "upgrades" part says and writes them to `out` (a temporary file
    // renamed over). Returns how many toys have a list; 0 (and nothing written) when the disc does not have them.
    int ExtractUpgrades(
        std::string_view game, const collection::Json& manifest, const Disc& disc, const std::filesystem::path& out);

    // Reads the creation parts' names as the manifest's "parts" part says (Imaginators) and writes them to `out`.
    // Returns how many parts have a name; 0 (and nothing written) when the disc does not have them.
    int ExtractPartNames(
        std::string_view game, const collection::Json& manifest, const Disc& disc, const std::filesystem::path& out);
}
