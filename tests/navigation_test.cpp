// ArkeyanLauncherTest's checks for the navigation model (launcher/ui/components/navigation.h, NavPick): where the D-pad goes
// from a stop, on plain boxes. Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/ui/components/navigation.h"
#include <cstdio>
#include <vector>

namespace
{
    int g_failures = 0;

#define NAV_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    using ui::NavBox;
    using ui::NavDir;

    NavBox Box(uint32_t id, float x, float y, float w, float h, int column = -1, uint32_t root = 1)
    {
        NavBox box;
        box.id = id;
        box.x0 = x;
        box.y0 = y;
        box.x1 = x + w;
        box.y1 = y + h;
        box.column = column;
        box.root = root;
        return box;
    }

    uint32_t Pick(const std::vector<NavBox>& boxes, uint32_t from, NavDir dir) { return ui::NavPick(boxes, from, dir); }
}

int NavigationChecks()
{
    g_failures = 0;
    // A settings card: rows of one control each at the right, a hairline between them.
    {
        std::vector<NavBox> rows = { Box(1, 500, 0, 200, 40), Box(2, 500, 64, 200, 40), Box(3, 560, 128, 140, 40) };
        NAV_EXPECT(Pick(rows, 1, NavDir::Down) == 2);
        NAV_EXPECT(Pick(rows, 2, NavDir::Down) == 3); // narrower, still under it
        NAV_EXPECT(Pick(rows, 3, NavDir::Up) == 2);
        NAV_EXPECT(Pick(rows, 3, NavDir::Down) == 0); // the last row: the focus stays
        NAV_EXPECT(Pick(rows, 1, NavDir::Up) == 0);
        NAV_EXPECT(Pick(rows, 2, NavDir::Right) == 0); // nothing beside it
        NAV_EXPECT(Pick(rows, 99, NavDir::Down) == 0); // a stop the model doesn't know
    }
    // A grid (the shelf): up and down keep the column, left and right stay in the row.
    {
        std::vector<NavBox> grid;
        for (uint32_t row = 0; row < 3; row++)
            for (uint32_t column = 0; column < 4; column++)
                grid.push_back(Box(10 * (row + 1) + column, float(column) * 110, float(row) * 150, 100, 140));
        NAV_EXPECT(Pick(grid, 12, NavDir::Down) == 22);
        NAV_EXPECT(Pick(grid, 22, NavDir::Down) == 32);
        NAV_EXPECT(Pick(grid, 33, NavDir::Up) == 23);
        NAV_EXPECT(Pick(grid, 21, NavDir::Right) == 22);
        NAV_EXPECT(Pick(grid, 21, NavDir::Left) == 20);
        NAV_EXPECT(Pick(grid, 20, NavDir::Left) == 0); // the row's first: no wrap
        NAV_EXPECT(Pick(grid, 13, NavDir::Right) == 0);
        // The last row shorter: down from a column it lacks goes to its nearest card.
        std::vector<NavBox> ragged = { Box(1, 0, 0, 100, 140), Box(2, 110, 0, 100, 140), Box(3, 220, 0, 100, 140), Box(4, 0, 150, 100, 140) };
        NAV_EXPECT(Pick(ragged, 3, NavDir::Down) == 4);
    }
    // A wide field over a row of buttons: the row's first, then the one it had last.
    {
        std::vector<NavBox> boxes = { Box(1, 0, 0, 600, 40), Box(2, 0, 60, 120, 40), Box(3, 140, 60, 120, 40), Box(4, 280, 60, 120, 40) };
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 2);
        boxes[2].seen = 5; // the focus was on the second button, then went up
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 3);
        NAV_EXPECT(Pick(boxes, 4, NavDir::Up) == 1);
    }
    // Tabs over a card: up from a control under no tab goes to the tab the row had last (the selected one).
    {
        std::vector<NavBox> boxes = { Box(1, 0, 0, 80, 40), Box(2, 120, 0, 80, 40), Box(3, 240, 0, 80, 40), Box(4, 500, 80, 200, 40) };
        NAV_EXPECT(Pick(boxes, 4, NavDir::Up) == 3); // never seen: the nearest across
        boxes[1].seen = 7;
        NAV_EXPECT(Pick(boxes, 4, NavDir::Up) == 2);
        NAV_EXPECT(Pick(boxes, 2, NavDir::Down) == 4);
        NAV_EXPECT(Pick(boxes, 1, NavDir::Right) == 2);
    }
    // A Back button raised onto the title's line, the tabs under the title at the left: down keeps Back's column (the wide
    // row under the tabs), the tabs are not under it; with a tab under it, that tab.
    {
        std::vector<NavBox> boxes = { Box(1, 600, 0, 90, 40), Box(2, 0, 70, 80, 40), Box(3, 120, 70, 80, 40), Box(4, 0, 140, 700, 40) };
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 4);
        NAV_EXPECT(Pick(boxes, 2, NavDir::Up) == 1);
        boxes.push_back(Box(5, 580, 70, 60, 40));
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 5);
    }
    // Add a figure: the type select over the versions panel, the catalogue's nearer row at its left. Up from the panel's row
    // goes to the select straight over it.
    {
        std::vector<NavBox> boxes = { Box(1, 726, 139, 200, 40), Box(2, 96, 190, 118, 137), Box(3, 226, 190, 118, 137),
            Box(4, 648, 274, 272, 56) };
        NAV_EXPECT(Pick(boxes, 4, NavDir::Up) == 1);
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 4);
    }
    // The game page: its actions (column 0) and its portal (column 1); up and down stay in their column even when the other
    // one's stop is nearer, left and right cross over.
    {
        std::vector<NavBox> boxes = { Box(1, 0, 300, 300, 60, 0), Box(2, 0, 380, 140, 32, 0), Box(3, 400, 320, 200, 60, 1),
            Box(4, 400, 390, 200, 60, 1), Box(5, 0, 460, 140, 32, 0) };
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 2);
        NAV_EXPECT(Pick(boxes, 2, NavDir::Down) == 5);
        NAV_EXPECT(Pick(boxes, 3, NavDir::Down) == 4);
        NAV_EXPECT(Pick(boxes, 4, NavDir::Down) == 0);
        NAV_EXPECT(Pick(boxes, 1, NavDir::Right) == 3);
        NAV_EXPECT(Pick(boxes, 4, NavDir::Left) == 2);
    }
    // A dialog over the page: a move never leaves the window it starts in.
    {
        std::vector<NavBox> boxes = { Box(1, 0, 0, 200, 40, -1, 1), Box(2, 0, 60, 200, 40, -1, 2), Box(3, 0, 120, 200, 40, -1, 1),
            Box(4, 220, 0, 100, 40, -1, 2) };
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 3);
        NAV_EXPECT(Pick(boxes, 1, NavDir::Right) == 0);
        NAV_EXPECT(Pick(boxes, 2, NavDir::Up) == 4);
    }
    // A side panel: right from a list row goes to the panel's stop nearest in height.
    {
        std::vector<NavBox> boxes = { Box(1, 0, 0, 400, 50), Box(2, 0, 60, 400, 50), Box(3, 0, 120, 400, 50), Box(4, 450, 10, 200, 40),
            Box(5, 450, 110, 200, 40) };
        NAV_EXPECT(Pick(boxes, 3, NavDir::Right) == 5);
        NAV_EXPECT(Pick(boxes, 1, NavDir::Right) == 4);
        NAV_EXPECT(Pick(boxes, 5, NavDir::Left) == 3);
        NAV_EXPECT(Pick(boxes, 2, NavDir::Down) == 3); // the panel's stop beside it is not the row under it
    }
    // A dialog's scrolling body (its visible part 0..300) over its buttons (320): down from the buttons never goes into the
    // body's hidden part; in the body, down from its last shown row goes on into it (the body scrolls).
    {
        std::vector<NavBox> boxes = { Box(1, 0, 200, 300, 40), Box(2, 0, 260, 300, 40), Box(3, 0, 340, 300, 40), Box(4, 0, 400, 300, 40),
            Box(5, 400, 320, 100, 40) };
        for (size_t i = 0; i < 4; i++)
        {
            boxes[i].clipY0 = 0;
            boxes[i].clipY1 = 300;
            boxes[i].window = 2; // the body; the buttons are the dialog's own
        }
        NAV_EXPECT(Pick(boxes, 5, NavDir::Down) == 0);
        NAV_EXPECT(Pick(boxes, 2, NavDir::Down) == 3);
        NAV_EXPECT(Pick(boxes, 5, NavDir::Up) == 2);
    }
    // The collection: the portal's select over the seats, the shelf (its own list, scrolled: its first row hidden above its
    // visible part, though laid out beside the seats). Down from the select goes to a seat, never to the hidden card, even one
    // the focus was on before.
    {
        std::vector<NavBox> boxes = { Box(1, 413, 108, 164, 40), Box(2, 302, 160, 160, 72), Box(3, 478, 160, 160, 72),
            Box(4, 302, 186, 129, 169), Box(5, 302, 366, 129, 169) };
        for (size_t i = 3; i < 5; i++)
        {
            boxes[i].clipY0 = 362;
            boxes[i].clipY1 = 658;
            boxes[i].window = 2;
        }
        boxes[3].seen = 9;
        NAV_EXPECT(Pick(boxes, 1, NavDir::Down) == 2);
        NAV_EXPECT(Pick(boxes, 1, NavDir::Right) == 0);
        NAV_EXPECT(Pick(boxes, 5, NavDir::Up) == 4); // in the shelf, up scrolls it back
    }
    printf("Navigation model: %d failures\n", g_failures);
    return g_failures;
}
