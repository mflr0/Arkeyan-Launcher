// ArkeyanLauncherTest's checks for the portal's seat rows (launcher/ui/seat_rows.h): one row while every label fits,
// two otherwise, the players' seats first. Called from launcher_test.cpp's main; returns the number of failures.
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
    // Imaginators (p1, p2, item, adventure, trap, vehicle1, vehicle2) at 900 px: on one row a seat is 114 px, 6 px of
    // label. Two rows: Player 1, Player 2, Vehicle 1, Vehicle 2, then Magic item, Adventure, Trap; every label whole.
    const std::vector<bool> imaginators = { true, true, false, false, false, true, true };
    Rows rows = launcher_ui::SeatRows(imaginators, 900, Gap, Label, LabelRoom);
    ROWS_EXPECT((rows == Rows{ { 0, 1, 5, 6 }, { 2, 3, 4 } }));
    for (const std::vector<size_t>& row : rows)
        ROWS_EXPECT(LabelRoom(launcher_ui::SeatWidth(900, Gap, row.size())) >= Label);
    // The widest page (1180 px) still cuts seven seats' labels.
    ROWS_EXPECT(launcher_ui::SeatRows(imaginators, 1180, Gap, Label, LabelRoom).size() == 2);
    // Spyro's Adventure: four seats fit on one row, in the game's order.
    ROWS_EXPECT(
        (launcher_ui::SeatRows({ true, true, false, false }, 900, Gap, Label, LabelRoom) == Rows{ { 0, 1, 2, 3 } }));
    // SuperChargers (p1, vehicle1, p2, vehicle2, item, trap) at 900 px: two rows.
    ROWS_EXPECT((launcher_ui::SeatRows({ true, true, true, true, false, false }, 900, Gap, Label, LabelRoom) ==
                 Rows{ { 0, 1, 2, 3 }, { 4, 5 } }));
    // Only players' seats: one row, even cut.
    ROWS_EXPECT((launcher_ui::SeatRows({ true, true }, 100, Gap, Label, LabelRoom) == Rows{ { 0, 1 } }));
    ROWS_EXPECT(launcher_ui::SeatWidth(900, Gap, 0) == 0);
    return g_failures;
}
