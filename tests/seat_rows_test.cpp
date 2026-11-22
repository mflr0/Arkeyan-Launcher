// ArkeyanLauncherTest's checks for the portal's seat rows (launcher/ui/seat_rows.h): one row while every label fits,
// two otherwise, the players' seats first, more when a narrow window leaves a label less than its least room. Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/ui/seat_rows.h"
#include <cstdio>
#include <vector>

namespace
{
    int g_failures = 0;

#define ROWS_EXPECT(condition)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition);                                  \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

    using Rows = std::vector<std::vector<size_t>>;

    // A seat's label room: its padding, picture and gap (84), and the X on an occupied seat (24).
    float LabelRoom(float seatWidth) { return seatWidth - 84 - 24; }
}

int SeatRowsChecks()
{
    g_failures = 0;
    constexpr float Gap = 16, Label = 80; // about "MAGIC ITEM" in the caption style
    constexpr float Least = 64; // the page's least label room (a main button's height)
    // Imaginators (p1, p2, item, adventure, trap, vehicle1, vehicle2) at 900 px: on one row a seat is 114 px, 6 px of
    // label. Two rows: Player 1, Player 2, Vehicle 1, Vehicle 2, then Magic item, Adventure, Trap; every label whole.
    const std::vector<bool> imaginators = { true, true, false, false, false, true, true };
    Rows rows = launcher_ui::SeatRows(imaginators, 900, Gap, Label, Least, LabelRoom);
    ROWS_EXPECT((rows == Rows{ { 0, 1, 5, 6 }, { 2, 3, 4 } }));
    for (const std::vector<size_t>& row : rows)
        ROWS_EXPECT(LabelRoom(launcher_ui::SeatWidth(900, Gap, row.size())) >= Label);
    // The widest page (1180 px) still cuts seven seats' labels.
    ROWS_EXPECT(launcher_ui::SeatRows(imaginators, 1180, Gap, Label, Least, LabelRoom).size() == 2);
    // Spyro's Adventure: four seats fit on one row, in the game's order.
    ROWS_EXPECT((launcher_ui::SeatRows({ true, true, false, false }, 900, Gap, Label, Least, LabelRoom) ==
                 Rows{ { 0, 1, 2, 3 } }));
    // SuperChargers (p1, vehicle1, p2, vehicle2, item, trap) at 900 px: two rows.
    ROWS_EXPECT((launcher_ui::SeatRows({ true, true, true, true, false, false }, 900, Gap, Label, Least, LabelRoom) ==
                 Rows{ { 0, 1, 2, 3 }, { 4, 5 } }));
    // Only players' seats: one row while the labels fit, one seat a row when even two would cut them.
    ROWS_EXPECT((launcher_ui::SeatRows({ true, true }, 400, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1 } }));
    ROWS_EXPECT((launcher_ui::SeatRows({ true, true }, 100, Gap, Label, Least, LabelRoom) == Rows{ { 0 }, { 1 } }));

    // A 640 px window: four seats on a row leave a label 40 px, five 7 px. The rows split again, in order, evenly.
    const std::vector<bool> four = { true, true, true, true }, five = { true, true, true, true, true };
    ROWS_EXPECT((launcher_ui::SeatRows(four, 640, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1 }, { 2, 3 } }));
    ROWS_EXPECT((launcher_ui::SeatRows(five, 640, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2 }, { 3, 4 } }));
    ROWS_EXPECT((launcher_ui::SeatRows(imaginators, 640, Gap, Label, Least, LabelRoom) ==
                 Rows{ { 0, 1 }, { 5, 6 }, { 2, 3, 4 } }));
    for (const std::vector<bool>* seats : { &four, &five, &imaginators })
        for (const std::vector<size_t>& row : launcher_ui::SeatRows(*seats, 640, Gap, Label, Least, LabelRoom))
            ROWS_EXPECT(LabelRoom(launcher_ui::SeatWidth(640, Gap, row.size())) >= Least);

    // 1024 and 1600 px: the layout of two rows at most, unchanged.
    const std::vector<bool> ssa = { true, true, false, false }, trapTeam = { true, true, false, false, false },
                            superChargers = { true, true, true, true, false, false };
    for (float wide : { 1024.0f, 1600.0f })
    {
        ROWS_EXPECT((launcher_ui::SeatRows(ssa, wide, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3 } }));
        ROWS_EXPECT((launcher_ui::SeatRows(trapTeam, wide, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3, 4 } }));
        ROWS_EXPECT((launcher_ui::SeatRows(four, wide, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3 } }));
        ROWS_EXPECT((launcher_ui::SeatRows(five, wide, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3, 4 } }));
    }
    ROWS_EXPECT((
        launcher_ui::SeatRows(imaginators, 1024, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 5, 6 }, { 2, 3, 4 } }));
    ROWS_EXPECT(
        (launcher_ui::SeatRows(superChargers, 1024, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3 }, { 4, 5 } }));
    ROWS_EXPECT(
        (launcher_ui::SeatRows(imaginators, 1600, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3, 4, 5, 6 } }));
    ROWS_EXPECT(
        (launcher_ui::SeatRows(superChargers, 1600, Gap, Label, Least, LabelRoom) == Rows{ { 0, 1, 2, 3, 4, 5 } }));
    ROWS_EXPECT(launcher_ui::SeatWidth(900, Gap, 0) == 0);
    return g_failures;
}
