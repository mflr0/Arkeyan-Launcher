// The navigation model (navigation.h): the stops, the moves between them, the pad actions and their legend.
#include "launcher/ui/components/navigation.h"
#include "launcher/i18n.h"
#include <imgui_internal.h> // the move request, FocusItem
#include <unordered_map>
#include <string_view>

namespace ui
{
    namespace
    {
        struct Hint
        {
            PadButton button;
            std::string label, key;
            uint32_t root;
        };
        std::vector<NavBox> g_stops, g_previous;
        std::unordered_map<uint32_t, uint64_t> g_seen;
        uint64_t g_frame = 0;
        uint32_t g_target = 0;
        int g_column = -1;
        uint32_t g_join = 0; // NavJoin's window, 0: none
        std::vector<Hint> g_hints; // this frame's pad actions, scoped to their top window

        uint32_t CurrentRoot()
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            return window != nullptr && window->RootWindow != nullptr ? window->RootWindow->ID : 0;
        }
    }

    bool HasInput()
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        if (ImGui::IsAnyItemActive())
            return false;
        if (g.OpenPopupStack.Size == 0)
            return true;
        ImGuiWindow* top = g.OpenPopupStack.back().Window;
        return top != nullptr && top->RootWindow != nullptr && top->RootWindow->ID == CurrentRoot();
    }

    namespace
    {
        ImGuiKey PadKey(PadButton button)
        {
            switch (button)
            {
            case PadButton::South: return ImGuiKey_GamepadFaceDown;
            case PadButton::East: return ImGuiKey_GamepadFaceRight;
            case PadButton::West: return ImGuiKey_GamepadFaceLeft;
            case PadButton::North: return ImGuiKey_GamepadFaceUp;
            case PadButton::LeftBumper: return ImGuiKey_GamepadL1;
            case PadButton::RightBumper: return ImGuiKey_GamepadR1;
            case PadButton::Start: return ImGuiKey_GamepadStart;
            case PadButton::Back: return ImGuiKey_GamepadBack;
            case PadButton::LeftTrigger: return ImGuiKey_GamepadL2;
            case PadButton::RightTrigger: return ImGuiKey_GamepadR2;
            }
            return ImGuiKey_None;
        }
    }

    void NavNewFrame()
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        g_frame++;
        g_previous.swap(g_stops);
        g_stops.clear();
        g_hints.clear();
        g_column = -1;
        g_join = 0;
        g_target = 0;
        if (g.NavId != 0 && g.NavCursorVisible)
            g_seen[g.NavId] = g_frame;
        // A move by the arrows, the D-pad or the stick (not Tab, not a page or edge jump, not the focus API's).
        constexpr ImGuiNavMoveFlags Other = ImGuiNavMoveFlags_IsTabbing | ImGuiNavMoveFlags_FocusApi |
                                            ImGuiNavMoveFlags_IsPageMove | ImGuiNavMoveFlags_ScrollToEdgeY |
                                            ImGuiNavMoveFlags_Forwarded;
        if (!g.NavMoveSubmitted || (g.NavMoveFlags & Other) != 0 || g.NavMoveDir < ImGuiDir_Left ||
            g.NavMoveDir > ImGuiDir_Down)
            return;
        bool known = false;
        for (NavBox& box : g_previous)
        {
            const auto seen = g_seen.find(box.id);
            box.seen = seen != g_seen.end() ? seen->second : 0;
            known = known || box.id == g.NavId;
        }
        if (!known)
            return; // a stop this model doesn't know (a select's open list): ImGui's own move
        static const NavDir Dirs[] = { NavDir::Left, NavDir::Right, NavDir::Up, NavDir::Down };
        g_target = NavPick(g_previous, g.NavId, Dirs[g.NavMoveDir]);
        if (g_target == 0)
            ImGui::NavMoveRequestCancel(); // nothing that way: the focus stays (ImGui would guess a diagonal one)
    }

    void NavItem()
    {
        if (ImGui::GetItemFlags() & (ImGuiItemFlags_Disabled | ImGuiItemFlags_NoNav))
            return;
        ImGuiContext& g = *ImGui::GetCurrentContext();
        const ImRect& rect = g.LastItemData.NavRect;
        const ImRect& clip = g.CurrentWindow->ClipRect;
        const uint32_t id = g.LastItemData.ID;
        NavBox box;
        box.id = id;
        box.x0 = rect.Min.x;
        box.y0 = rect.Min.y;
        box.x1 = rect.Max.x;
        box.y1 = rect.Max.y;
        box.clipX0 = clip.Min.x;
        box.clipY0 = clip.Min.y;
        box.clipX1 = clip.Max.x;
        box.clipY1 = clip.Max.y;
        box.window = g.CurrentWindow->ID;
        box.root = g_join != 0 ? g_join : CurrentRoot();
        box.column = g_column;
        g_stops.push_back(box);
        if (id == g_target)
        {
            g_target = 0;
            FocusLastItem(true);
        }
        if (Ctx().testNavigationItems != nullptr)
            Ctx().testNavigationItems->push_back(id);
        if (Ctx().testStops != nullptr)
        {
            // Where it shows: a row half scrolled out of its list is judged by its visible part.
            ImRect shown = rect;
            shown.ClipWithFull(clip);
            const bool visible = (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible) != 0;
            Ctx().testStops->push_back(
                { id, box.root, visible ? shown.Min : rect.Min, visible ? shown.Max : rect.Max, visible });
        }
    }

    void NavColumn(int column) { g_column = column; }

    void NavJoin(const char* window)
    {
        ImGuiWindow* joined = window != nullptr ? ImGui::FindWindowByName(window) : nullptr;
        g_join = joined != nullptr ? joined->RootWindow->ID : 0;
    }

    std::span<const NavBox> NavStops() { return g_previous; }

    bool PadAction(PadButton button, const char* label, const char* key, int keyboard)
    {
        g_hints.push_back({ button, label, key != nullptr ? key : "", CurrentRoot() });
        if (!HasInput())
            return false;
        return ImGui::IsKeyPressed(PadKey(button), false) ||
               (keyboard != ImGuiKey_None && ImGui::IsKeyPressed(ImGuiKey(keyboard), false));
    }

    void PageLegend(const char* back, float width, bool pages, bool select)
    {
        const uint32_t root = CurrentRoot();
        std::vector<PadHint> actions;
        for (const Hint& hint : g_hints)
            if (hint.root == root)
                actions.push_back({ hint.button, hint.key.empty() ? nullptr : hint.key.c_str(), hint.label.c_str() });
        std::vector<PadHint> hints = LegendHints(actions, back, pages, select);
        for (PadHint& hint : hints)
        {
            if (hint.button == PadButton::LeftBumper && hint.label == std::string_view("Pages"))
                hint.label = Tr("Pages");
            else if (hint.button == PadButton::South && hint.label == std::string_view("Select"))
                hint.label = Tr("Select");
        }
        PadLegend(hints, Ctx().glyphs, width);
    }
}
