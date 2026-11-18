#include "launcher/game_chests.h"
#include "launcher/i18n.h"
#include "shared/collection/chests.h"
#include "shared/collection/save_backup.h"
#include <array>
#include <string>

namespace
{
    constexpr std::array<ChestKindRow, 9> ImaginatorsChests = { { { "imaginite", N_("Imaginite Chest") },
        { "copper", N_("Copper Chest") }, { "bronze", N_("Bronze Chest") }, { "silver", N_("Silver Chest") },
        { "gold", N_("Gold Chest") }, { "platinum", N_("Platinum Chest") },
        { "toy_bronze", N_("Bronze Mystery Chest") }, { "toy_silver", N_("Silver Mystery Chest") },
        { "toy_gold", N_("Gold Mystery Chest") } } };

    std::vector<std::string_view> Keys(std::string_view gameId)
    {
        std::vector<std::string_view> keys;
        for (const ChestKindRow& kind : ChestKinds(gameId))
            keys.push_back(kind.key);
        return keys;
    }

    const char* RequestFile = "chests_add.txt";
}

bool ChestsOffered(std::string_view gameId) { return gameId == "imaginators"; }

std::span<const ChestKindRow> ChestKinds(std::string_view gameId)
{
    if (gameId == "imaginators")
        return ImaginatorsChests;
    return {};
}

GameChests ReadGameChests(const std::filesystem::path& folder, std::string_view gameId)
{
    GameChests chests;
    const std::vector<std::string_view> keys = Keys(gameId);
    chests.waitingByKind.assign(keys.size(), 0);
    if (folder.empty() || keys.empty())
        return chests;
    std::error_code error;
    if (std::filesystem::exists(folder / "chests.txt", error))
        if (const auto status = chests::ParseStatus(savebackup::ReadFile(folder / "chests.txt"), keys))
        {
            chests.known = true;
            chests.slot = status->slot;
            chests.pile = status->Total();
        }
    if (std::filesystem::exists(folder / RequestFile, error))
        if (const auto request = chests::ParseRequest(savebackup::ReadFile(folder / RequestFile), keys))
        {
            chests.waiting = request->Total();
            chests.waitingSlot = request->slot;
            chests.waitingByKind = request->counts;
        }
    chests.room = chests.known && !chests.WaitingElsewhere() ? chests::RoomLeft(chests.pile, chests.waiting) : 0;
    return chests;
}

bool AddChests(const std::filesystem::path& folder, std::string_view gameId, const std::vector<int>& counts)
{
    const GameChests now = ReadGameChests(folder, gameId);
    const std::vector<std::string_view> keys = Keys(gameId);
    if (!now.known || now.WaitingElsewhere() || counts.size() != keys.size())
        return false;
    int asked = 0;
    for (int count : counts)
    {
        if (count < 0)
            return false;
        asked += count;
    }
    if (asked == 0 || asked > now.room)
        return false;
    // The waiting request is taken by a rename before it is merged, as the game claims it: when the game took it first,
    // the rename fails and only the new chests are asked for, so none is added twice.
    chests::Request request{ now.slot, std::vector<int>(keys.size(), 0) };
    const std::filesystem::path waiting = folder / RequestFile, merging = folder / "chests_add.merge";
    std::error_code error;
    if (now.waiting > 0)
    {
        std::filesystem::rename(waiting, merging, error);
        if (!error)
        {
            if (const auto taken = chests::ParseRequest(savebackup::ReadFile(merging), keys);
                taken && taken->slot == now.slot)
                request.counts = taken->counts;
            std::filesystem::remove(merging, error);
        }
    }
    for (size_t i = 0; i < counts.size(); i++)
        request.counts[i] += counts[i];
    const std::string text = chests::FormatRequest(request, keys);
    return savebackup::WriteFileAtomic(waiting, text.data(), text.size());
}

bool CancelChests(const std::filesystem::path& folder)
{
    std::error_code error;
    return !folder.empty() && std::filesystem::remove(folder / RequestFile, error) && !error;
}
