#pragma once

// The pad's and the arrow keys' way through a window: one model for every page, dialog and menu (docs/launcher_ui.md,
// "Navigation model"). Every control a component draws is a stop (Hit, the fields, the selects call NavItem); a move
// goes to the stop the model picks (NavPick) instead of ImGui's own guess. The page's pad actions (X, Y...) are declared
// where they are done (PadAction) and their hints are the legend's (PageLegend), so a hint never shows without its action.
#include "launcher/ui/components/feedback.h"
#include "launcher/ui/components/navigation_model.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui
{
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
    // `width` wide (< 0: the available width). `pages`: LB RB change the main page here unless tabs own them.
    // A null `back` omits B; `select` false omits A (a progress view with no controls).
    void PageLegend(const char* back, float width = -1, bool pages = false, bool select = true);
}
