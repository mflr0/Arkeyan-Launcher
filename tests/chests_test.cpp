// ArkeyanLauncherTest's checks for Imaginite chests (launcher/game_chests.h): the pile and the waiting chests read from the
// player folder, the room left, adding and cancelling. Scratch folders only. Returns the number of failures.
#include "launcher/game_chests.h"
#include "shared/collection/chests.h"
#include "shared/collection/save_backup.h"
#include <cstdio>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    int g_chestFailures = 0;
#define CHESTS_EXPECT(condition)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("FAIL chests line %d: %s\n", __LINE__, #condition);                                                 \
            g_chestFailures++;                                                                                         \
        }                                                                                                              \
    } while (0)

    void Write(const fs::path& file, const std::string& text)
    {
        CHESTS_EXPECT(savebackup::WriteFileAtomic(file, text.data(), text.size()));
    }

    std::vector<int> Counts(std::initializer_list<std::pair<size_t, int>> some)
    {
        std::vector<int> counts(ChestKinds("imaginators").size(), 0);
        for (const auto& [kind, count] : some)
            counts[kind] = count;
        return counts;
    }
}

int ChestsChecks(const fs::path& scratch)
{
    std::error_code error;
    fs::remove_all(scratch, error);
    fs::create_directories(scratch, error);
    const fs::path request = scratch / "chests_add.txt";

    CHESTS_EXPECT(ChestsOffered("imaginators") && !ChestsOffered("superchargers") && !ChestsOffered("ssa"));
    CHESTS_EXPECT(ChestKinds("imaginators").size() == 9 && ChestKinds("imaginators")[0].key == "imaginite");
    CHESTS_EXPECT(ChestKinds("giants").empty());

    // Before the game read a pile: nothing known, nothing can be asked for.
    GameChests chests = ReadGameChests(scratch, "imaginators");
    CHESTS_EXPECT(!chests.known && chests.room == 0);
    CHESTS_EXPECT(!AddChests(scratch, "imaginators", Counts({ { 0, 1 } })));
    CHESTS_EXPECT(!fs::exists(request));

    // A pile of 4 in slot 1: room for 95.
    Write(scratch / "chests.txt", "slot=1\npile=4\npile.imaginite=3\npile.gold=1\npile.other=0\nlimit=99\n");
    chests = ReadGameChests(scratch, "imaginators");
    CHESTS_EXPECT(chests.known && chests.slot == 1 && chests.pile == 4 && chests.waiting == 0 && chests.room == 95);

    // 5 of the best and 20 of a lower one, then 10 more: they wait together and count against the 99.
    CHESTS_EXPECT(AddChests(scratch, "imaginators", Counts({ { 5, 5 }, { 1, 20 } })));
    CHESTS_EXPECT(AddChests(scratch, "imaginators", Counts({ { 5, 10 } })));
    chests = ReadGameChests(scratch, "imaginators");
    CHESTS_EXPECT(chests.waiting == 35 && chests.waitingSlot == 1 && chests.room == 60);
    CHESTS_EXPECT(chests.waitingByKind == Counts({ { 5, 15 }, { 1, 20 } }));
    CHESTS_EXPECT(!fs::exists(scratch / "chests_add.merge"));

    // More than the room, none, or a negative count: refused, the request unchanged.
    CHESTS_EXPECT(!AddChests(scratch, "imaginators", Counts({ { 0, 61 } })));
    CHESTS_EXPECT(!AddChests(scratch, "imaginators", Counts({})));
    CHESTS_EXPECT(!AddChests(scratch, "imaginators", Counts({ { 0, -1 }, { 1, 3 } })));
    CHESTS_EXPECT(AddChests(scratch, "imaginators", Counts({ { 0, 60 } })));
    CHESTS_EXPECT(ReadGameChests(scratch, "imaginators").room == 0);

    // Cancel: nothing waits any more; a second cancel finds nothing.
    CHESTS_EXPECT(CancelChests(scratch));
    CHESTS_EXPECT(!fs::exists(request));
    CHESTS_EXPECT(!CancelChests(scratch));
    CHESTS_EXPECT(ReadGameChests(scratch, "imaginators").room == 95);

    // The game took the request (renamed) just before an addition: only the new chests are asked for.
    CHESTS_EXPECT(AddChests(scratch, "imaginators", Counts({ { 2, 7 } })));
    fs::rename(request, scratch / "chests_add.claimed", error);
    CHESTS_EXPECT(!error);
    Write(scratch / "chests.txt", "slot=1\npile=4\npile.imaginite=3\npile.gold=1\npile.other=0\nlimit=99\n");
    {
        // The launcher's view was read while the 7 still waited: AddChests reads again, and the merge finds nothing.
        CHESTS_EXPECT(AddChests(scratch, "imaginators", Counts({ { 3, 2 } })));
        std::vector<std::string_view> keys;
        for (const ChestKindRow& kind : ChestKinds("imaginators"))
            keys.push_back(kind.key);
        const auto now = chests::ParseRequest(savebackup::ReadFile(request), keys);
        CHESTS_EXPECT(now && now->Total() == 2 && now->counts[3] == 2);
    }
    fs::remove(scratch / "chests_add.claimed", error);
    CHESTS_EXPECT(CancelChests(scratch));

    // Chests waiting for slot 0 while slot 1 was played last: they still wait, and no more can be asked for.
    Write(request, "slot=0\nimaginite=4\n");
    chests = ReadGameChests(scratch, "imaginators");
    CHESTS_EXPECT(chests.WaitingElsewhere() && chests.waiting == 4 && chests.waitingSlot == 0 && chests.room == 0);
    CHESTS_EXPECT(!AddChests(scratch, "imaginators", Counts({ { 0, 1 } })));
    CHESTS_EXPECT(savebackup::ReadFile(request) == "slot=0\nimaginite=4\n");

    // A full pile.
    CHESTS_EXPECT(CancelChests(scratch));
    Write(scratch / "chests.txt", "slot=2\npile=99\npile.imaginite=99\nlimit=99\n");
    CHESTS_EXPECT(ReadGameChests(scratch, "imaginators").room == 0);

    fs::remove_all(scratch, error);
    printf("chests: %d failures\n", g_chestFailures);
    return g_chestFailures;
}
