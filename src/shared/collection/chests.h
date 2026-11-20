#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Chests the player adds from the launcher, for a game whose save keeps a pile of chests to open (Imaginators: its
// adapter names the kinds; docs/getting-started/settings.md, "chests.txt"). Two small text files in the player folder,
// next to presence.txt:
// - chests.txt (the game -> the launcher): the save slot played last and its pile, written each time the game reads it;
// - chests_add.txt (the launcher -> the game): the chests to add to that slot, until the game claims them.
// The save itself is never written from outside the game: the game adds the chests through its own code and saves them.
namespace chests
{
    // At most this many chests in the pile, those waiting in chests_add.txt included.
    constexpr int PileLimit = 99;

    // chests_add.txt: `slot=<n>` and one `<kind>=<n>` line per kind (the kinds in the game's order).
    struct Request
    {
        int slot = -1;
        std::vector<int> counts; // one per kind
        int Total() const;
        bool operator==(const Request&) const = default;
    };

    // chests.txt: `slot=<n>`, `pile=<total>`, one `pile.<kind>=<n>` line per kind, `pile.other=<n>` (chests of no kind
    // the launcher offers), `limit=<n>`.
    struct Status
    {
        int slot = -1;
        std::vector<int> pile; // one per kind
        int other = 0;
        int Total() const;
        bool operator==(const Status&) const = default;
    };

    // Unknown keys are skipped; a count that is not a number, or below 0, is 0; above the limit, the limit. nullopt
    // without a slot of 0 or more.
    std::optional<Request> ParseRequest(std::string_view text, std::span<const std::string_view> kinds);
    std::string FormatRequest(const Request& request, std::span<const std::string_view> kinds);
    std::optional<Status> ParseStatus(std::string_view text, std::span<const std::string_view> kinds);
    std::string FormatStatus(const Status& status, std::span<const std::string_view> kinds, int limit = PileLimit);

    // The chests that can still be asked for: the limit less the pile and those already waiting, never below 0.
    int RoomLeft(int pile, int waiting, int limit = PileLimit);
    // What to add for `request` to a pile of `pile` chests: each kind in order, cut at what the limit leaves.
    std::vector<int> Plan(const Request& request, int pile, int limit = PileLimit);

    // The two files in one player folder. An empty folder (none known) writes and claims nothing.
    class Folder
    {
    public:
        explicit Folder(std::filesystem::path folder) : m_folder(std::move(folder)) {}
        // chests.txt, atomically (a temporary sibling, then a rename). False when nothing changed or the write failed.
        bool WriteStatus(const Status& status, std::span<const std::string_view> kinds);
        // The request waiting for `slot`. It is renamed before it is read, then removed: once the launcher deleted it (a
        // cancel), the rename fails and nothing is added, so a cancel and an addition never both happen. A request for
        // another slot stays waiting; a file that is not a request is left for the launcher to replace.
        std::optional<Request> Claim(int slot, std::span<const std::string_view> kinds);

    private:
        std::filesystem::path m_folder;
        std::string m_written;
    };
}
