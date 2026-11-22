#pragma once

// The shelf's smart search and sort, shared by the Toy Box and Arkeyan Launcher (docs/runtime/host/collection.md "Search and
// sort"): one function says whether a card matches what the player typed, and Shelf cuts a list into the shelf's
// sections in the player's sort.
#include "collection.h"
#include <string>
#include <vector>

namespace collection
{
    // Lower case with the accents taken off (UTF-8 in, ASCII for Latin letters out): "Électricité" gives "electricite".
    std::string Fold(const std::string& utf8);

    // Whether `card` (both halves of a swapper) matches `query`. The query is words, all of which must match (a word
    // that does not match anything makes the card drop out). A word matches, in English or in French, whichever
    // language the app shows:
    //   - the start of a name word: the figure's label, nickname, name, version, edition or file name ("gill", "spy");
    //   - an element ("fire", "feu", "lumiere"), a game ("giants", "swap", "ssa", "spyro"), a toy type ("item", "objet",
    //     "adventure", "trap", "mini", "swapper"), a version ("legendary", "lightcore", "dark", "gold", "cristal");
    //   - "level 10", "niveau 10", "lvl 10", "lv10": a character at that level (the level the card shows in `game`, or its
    //     own level); "level" alone: any toy that has a level.
    // Case and accents do not matter; a word of 5 letters or more may have one typo ("giatns", "lightcroe", "dragn").
    // An empty query matches everything. `game` ("ssa", "swapforce"...) fixes the level cap the card shows.
    bool SearchMatches(const std::string& query, const Card& card, const std::string& game);

    // How the shelf is ordered, as the menus list it. Game, Element and Category cut it into titled sections, each in
    // the Recent order: favourites first, then what the game took last, then by element.
    enum class Sort
    {
        Game, // the game a figure came out with
        Recent,
        Element,
        Category, // the toys' families
        Name,
        Level,
        Favorites, // favorites first, then the Recent order
        Count
    };
    constexpr Sort DefaultSort = Sort::Game;
    const char* SortId(Sort sort); // "game", "recent", "element", "category", "name", "level", "favorites"
    Sort ParseSort(const std::string& id); // DefaultSort when unknown
    // The sort's name in the apps' menus and tags ("Recent" / "Récents"), the same words in both apps.
    std::string SortLabel(Sort sort, bool french = false);

    // "12 figures" and "1 figure"; "12 figurines" and, as in French, "0 figurine" and "1 figurine": the count of the shelf.
    std::string FigureCount(int count, bool french = false);

    // Group(list, Smart, ...) in the player's sort:
    //   - Game: a Section::Kind::Game per game a figure came out with (Catalogue::Origin: a re-release's own game), that
    //     of `first` (empty: `game`) ahead, then the release order;
    //   - Element: a Kind::Element per element in the menus' order, then a Kind::Category per family for what has none;
    //   - Category: a Kind::Category per family, named by its Category: Core (every playable character but minis),
    //     Mini, CreationCrystal, Trap, Vehicle, MagicItem, Adventure, Trophy, Villain;
    //   - Name (A to Z), Level (highest first, toys with no level last), ties by name; Recent; Favorites: one
    //     Kind::Shelf, untitled.
    // No section is empty. Unknown and "other games" toys keep their sections, after those.
    std::vector<Section> Shelf(
        const std::vector<Figure>& list, Sort sort, const std::string& game, const std::string& first = {});
}
