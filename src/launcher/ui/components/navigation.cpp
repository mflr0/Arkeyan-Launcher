// The navigation model (navigation.h): the stops, the moves between them, the pad actions and their legend.
#include "launcher/ui/components/navigation.h"
#include "launcher/i18n.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui_internal.h> // the move request, FocusItem
#include <unordered_map>

namespace ui
{
    namespace
    {
        float MidX(const NavBox& b) { return (b.x0 + b.x1) / 2; }
        float MidY(const NavBox& b) { return (b.y0 + b.y1) / 2; }
        float OverlapX(const NavBox& a, const NavBox& b) { return std::min(a.x1, b.x1) - std::max(a.x0, b.x0); }
        float GapX(const NavBox& a, const NavBox& b) { return std::max(0.0f, std::max(b.x0 - a.x1, a.x0 - b.x1)); }
        // Where a stop is as seen: inside its window's visible part (a stop scrolled out sits on that part's edge).
        NavBox Seen(const NavBox& b)
        {
            NavBox seen = b;
            seen.x0 = std::clamp(b.x0, b.clipX0, b.clipX1);
            seen.x1 = std::clamp(b.x1, b.clipX0, b.clipX1);
            seen.y0 = std::clamp(b.y0, b.clipY0, b.clipY1);
            seen.y1 = std::clamp(b.y1, b.clipY0, b.clipY1);
            return seen;
        }
        // One row: they share at least half of the shorter one's height.
        bool SameRow(const NavBox& a, const NavBox& b)
        {
            const float shared = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
            return shared > 0.5f * std::min(a.y1 - a.y0, b.y1 - b.y0);
        }
    }

    uint32_t NavPick(std::span<const NavBox> boxes, uint32_t from, NavDir dir)
    {
        const NavBox* source = nullptr;
        for (const NavBox& box : boxes)
            if (box.id == from)
                source = &box;
        if (source == nullptr)
            return 0;
        const NavBox& s = *source;
        std::vector<const NavBox*> ahead;
        for (const NavBox& b : boxes)
        {
            if (b.id == s.id || b.root != s.root)
                continue;
            // That way as laid out and as seen; rows as seen (a stop scrolled out of its list is in no row of the page).
            const NavBox seen = Seen(b), from = Seen(s);
            const bool row = SameRow(seen, from);
            switch (dir)
            {
            case NavDir::Down: if (!row && MidY(b) > MidY(s) && MidY(seen) > MidY(from)) ahead.push_back(&b); break;
            case NavDir::Up: if (!row && MidY(b) < MidY(s) && MidY(seen) < MidY(from)) ahead.push_back(&b); break;
            case NavDir::Right: if (MidX(b) > MidX(s) && MidX(seen) > MidX(from) && OverlapX(b, s) <= 0) ahead.push_back(&b); break;
            case NavDir::Left: if (MidX(b) < MidX(s) && MidX(seen) < MidX(from) && OverlapX(b, s) <= 0) ahead.push_back(&b); break;
            }
        }
        // The source's own list scrolls on first: when it has stops hidden that way, the others wait.
        auto hidden = [&](const NavBox& b) {
            switch (dir)
            {
            case NavDir::Down: return b.y1 > b.clipY1 + 0.5f;
            case NavDir::Up: return b.y0 < b.clipY0 - 0.5f;
            case NavDir::Right: return b.x1 > b.clipX1 + 0.5f;
            default: return b.x0 < b.clipX0 - 0.5f;
            }
        };
        if (std::any_of(ahead.begin(), ahead.end(), [&](const NavBox* b) { return b->window == s.window && hidden(*b); }))
            std::erase_if(ahead, [&](const NavBox* b) { return b->window != s.window; });
        auto remembered = [](const std::vector<const NavBox*>& set) {
            const NavBox* best = nullptr;
            for (const NavBox* b : set)
                if (b->seen > 0 && (best == nullptr || b->seen > best->seen))
                    best = b;
            return best;
        };
        if (dir == NavDir::Left || dir == NavDir::Right)
        {
            // The nearest stop of the row that way; else the nearest that way at all, by height first (another column).
            const NavBox* best = nullptr;
            float bestScore = FLT_MAX;
            for (const NavBox* b : ahead)
            {
                const float score = SameRow(Seen(*b), Seen(s)) ? GapX(*b, s) : 1e6f + std::abs(MidY(*b) - MidY(s)) * 100 + GapX(*b, s);
                if (score < bestScore)
                {
                    bestScore = score;
                    best = b;
                }
            }
            return best != nullptr ? best->id : 0;
        }
        // Up / Down: the page's own column only, when it has columns.
        if (s.column >= 0)
            std::erase_if(ahead, [&](const NavBox* b) { return b->column != s.column; });
        if (ahead.empty())
            return 0;
        // The nearest row: the stop whose near edge is closest as seen (a hidden one on its list's edge), then as laid out,
        // and every stop in its row.
        auto gap = [&](const NavBox& a, const NavBox& b) { return dir == NavDir::Down ? b.y0 - a.y1 : a.y0 - b.y1; };
        auto closer = [&](const NavBox& b, const NavBox& than) {
            const float seen = gap(Seen(s), Seen(b)), other = gap(Seen(s), Seen(than));
            if (std::abs(seen - other) > 0.5f)
                return seen < other;
            if (std::abs(gap(s, b) - gap(s, than)) > 0.5f)
                return gap(s, b) < gap(s, than);
            return GapX(b, s) < GapX(than, s);
        };
        // The column kept: the nearest of the stops straight that way when there are some (a field over a list beside it goes
        // to the field's own column, not to the list's nearer row), else the nearest of all.
        const bool straight = std::any_of(ahead.begin(), ahead.end(), [&](const NavBox* b) { return OverlapX(*b, s) > 0; });
        const NavBox* nearest = nullptr;
        for (const NavBox* b : ahead)
            if ((!straight || OverlapX(*b, s) > 0) && (nearest == nullptr || closer(*b, *nearest)))
                nearest = b;
        std::vector<const NavBox*> row, under;
        for (const NavBox* b : ahead)
            if (b == nearest || SameRow(Seen(*b), Seen(*nearest)))
            {
                row.push_back(b);
                if (OverlapX(*b, s) > 0)
                    under.push_back(b);
            }
        auto first = [](const std::vector<const NavBox*>& set) {
            return *std::min_element(set.begin(), set.end(), [](const NavBox* a, const NavBox* b) { return a->x0 < b->x0; });
        };
        // The column kept: the one stop under (or over) the source.
        if (under.size() == 1)
            return under.front()->id;
        // A wide source over several stops: the one the row had last, else the first of them.
        if (under.size() > 1)
        {
            const NavBox* last = remembered(under);
            return (last != nullptr ? last : first(under))->id;
        }
        // Nothing straight under: the row's remembered stop, else the nearest across.
        if (const NavBox* last = remembered(row))
            return last->id;
        const NavBox* best = row.front();
        for (const NavBox* b : row)
            if (GapX(*b, s) < GapX(*best, s))
                best = b;
        return best->id;
    }

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
        std::vector<Hint> g_hints, g_shownHints; // this frame's pad actions, last frame's (the legend is drawn before some)

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
        g_shownHints.swap(g_hints);
        g_hints.clear();
        g_column = -1;
        g_join = 0;
        g_target = 0;
        if (g.NavId != 0 && g.NavCursorVisible)
            g_seen[g.NavId] = g_frame;
        // A move by the arrows, the D-pad or the stick (not Tab, not a page or edge jump, not the focus API's).
        constexpr ImGuiNavMoveFlags Other = ImGuiNavMoveFlags_IsTabbing | ImGuiNavMoveFlags_FocusApi | ImGuiNavMoveFlags_IsPageMove |
            ImGuiNavMoveFlags_ScrollToEdgeY | ImGuiNavMoveFlags_Forwarded;
        if (!g.NavMoveSubmitted || (g.NavMoveFlags & Other) != 0 || g.NavMoveDir < ImGuiDir_Left || g.NavMoveDir > ImGuiDir_Down)
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
            Ctx().testStops->push_back({ id, box.root, rect.Min, rect.Max, (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible) != 0 });
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
        return ImGui::IsKeyPressed(PadKey(button), false) || (keyboard != ImGuiKey_None && ImGui::IsKeyPressed(ImGuiKey(keyboard), false));
    }

    void PageLegend(const char* back, float width, bool pages)
    {
        const uint32_t root = CurrentRoot();
        std::vector<PadHint> hints;
        if (pages)
            hints.push_back({ PadButton::LeftBumper, nullptr, Tr("Pages"), true });
        // The window's own actions (last frame's: some are declared after the legend is drawn); LT and RT with one label are one hint.
        for (size_t i = 0; i < g_shownHints.size(); i++)
        {
            const Hint& hint = g_shownHints[i];
            if (hint.root != root)
                continue;
            if (!hints.empty() && hints.back().label == hint.label && int(hints.back().button) + 1 == int(hint.button))
            {
                hints.back().withNext = true;
                continue;
            }
            hints.push_back({ hint.button, hint.key.empty() ? nullptr : hint.key.c_str(), hint.label.c_str() });
        }
        hints.push_back({ PadButton::South, "Enter", Tr("Select") });
        hints.push_back({ PadButton::East, "Esc", back });
        PadLegend(hints, Ctx().glyphs, width);
    }
}
