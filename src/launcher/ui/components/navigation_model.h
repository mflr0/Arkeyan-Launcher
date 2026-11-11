#pragma once

// Navigation and legend composition on plain values, independent of the window and renderer.
#include <cstdint>
#include <span>
#include <vector>

namespace ui
{
    // ---- The model, on plain boxes (tests/navigation_test.cpp). ----
    enum class NavDir
    {
        Left,
        Right,
        Up,
        Down
    };
    struct NavBox
    {
        uint32_t id = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // screen points
        // What its window shows (a scrolling list's visible part): a stop scrolled out of it is that way from the list's edge,
        // so a move from outside the list (a dialog's buttons under it) never goes into its hidden part.
        float clipX0 = -1e9f, clipY0 = -1e9f, clipX1 = 1e9f, clipY1 = 1e9f;
        uint32_t window =
            0; // its own window (a scrolling list): one with stops hidden that way scrolls on before the focus leaves it
        uint32_t root = 0; // its top window (a page, a dialog, a menu): a move stays in it
        int column = -1; // a page's own column (NavColumn), -1: none
        uint64_t seen = 0; // when it last had the focus (0: never): a row's remembered position
    };
    // Where a move from `from` goes, 0 for nowhere (the focus stays). Up / Down go to the nearest row beyond that has a stop
    // straight that way (else the nearest row), in the same column (NavColumn) when the page has columns: the stop under the
    // source when only one is, else the one the row last had the focus on, else its first. Left / Right stay in the row (the nearest stop), else the nearest stop that way
    // (another column, a side panel). A scrolling list with stops hidden that way keeps the focus (it scrolls).
    uint32_t NavPick(std::span<const NavBox> boxes, uint32_t from, NavDir dir);

    // A pad button by its place (South is A on Xbox, Cross on PlayStation, B on Nintendo).
    enum class PadButton
    {
        South,
        East,
        West,
        North,
        LeftBumper,
        RightBumper,
        Start,
        Back,
        LeftTrigger,
        RightTrigger
    };
    struct PadHint
    {
        PadButton button;
        const char* key; // the keyboard's key for it ("Enter"); nullptr: not shown on the keyboard
        const char* label; // what it does ("Select", "Back"), translated
        bool withNext = false; // the next button's cap too: LB RB, LT RT
    };
    // The current view's actions, paired where appropriate, followed by Select and Back when available.
    // A view with bumper actions owns LB / RB; the main-page hint is then omitted.
    std::vector<PadHint> LegendHints(
        std::span<const PadHint> actions, const char* back, bool pages, bool select = true);
}
