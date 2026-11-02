#pragma once

// The pad's and the arrow keys' way through a window: one model for every page, dialog and menu (docs/launcher_ui.md,
// "Navigation model"). Every control a component draws is a stop (Hit, the fields, the selects call NavItem); a move
// goes to the stop the model picks (NavPick) instead of ImGui's own guess. The page's pad actions (X, Y...) are declared
// where they are done (PadAction) and their hints are the legend's (PageLegend), so a hint never shows without its action.
#include "launcher/ui/components/feedback.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui
{
    // ---- The model, on plain boxes (tests/navigation_test.cpp). ----
    enum class NavDir { Left, Right, Up, Down };
    struct NavBox
    {
        uint32_t id = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // screen points
        // What its window shows (a scrolling list's visible part): a stop scrolled out of it is that way from the list's edge,
        // so a move from outside the list (a dialog's buttons under it) never goes into its hidden part.
        float clipX0 = -1e9f, clipY0 = -1e9f, clipX1 = 1e9f, clipY1 = 1e9f;
        uint32_t window = 0;  // its own window (a scrolling list): one with stops hidden that way scrolls on before the focus leaves it
        uint32_t root = 0;    // its top window (a page, a dialog, a menu): a move stays in it
        int column = -1;      // a page's own column (NavColumn), -1: none
        uint64_t seen = 0;    // when it last had the focus (0: never): a row's remembered position
    };
    // Where a move from `from` goes, 0 for nowhere (the focus stays). Up / Down go to the nearest row beyond that has a stop
    // straight that way (else the nearest row), in the same column (NavColumn) when the page has columns: the stop under the
    // source when only one is, else the one the row last had the focus on, else its first. Left / Right stay in the row (the nearest stop), else the nearest stop that way
    // (another column, a side panel). A scrolling list with stops hidden that way keeps the focus (it scrolls).
    uint32_t NavPick(std::span<const NavBox> boxes, uint32_t from, NavDir dir);

    // ---- The window's glue (ui::NewFrame calls NavNewFrame). ----
    void NavNewFrame();
    // The last item is a stop (when enabled). Hit calls it; a component built on an ImGui widget calls it after the widget.
    void NavItem();
    // The next items' column (the game page: its actions and its portal); -1 for none. Reset every frame.
    void NavColumn(int column);
    // The next items move with the stops of the top window named `window` (a band drawn as its own window over the page: the
    // update banner); nullptr: their own window's again. Reset every frame.
    void NavJoin(const char* window);
    // Last frame's stops (the screenshot test's pad walk).
    std::span<const NavBox> NavStops();
    // The current window has the input: no dialog or menu above it, nothing being typed in or held.
    bool HasInput();

    // ---- Pad actions and the legend. ----
    // The pad button's action on this page or dialog, `label` its hint (translated); `key` the keyboard's key named in the
    // legend and `keyboard` that key (ImGuiKey_None: the pad's only). True when pressed while the window has the input: no
    // dialog or menu over it, no field being typed in.
    bool PadAction(PadButton button, const char* label, const char* key = nullptr, int keyboard = 0);
    // The legend of the current window (a page or a dialog): its pad actions, then A Select and B `back`, at the cursor,
    // `width` wide (< 0: the available width). `pages`: LB RB change the main page here.
    void PageLegend(const char* back, float width = -1, bool pages = false);
}
