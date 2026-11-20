// What tells the player something (feedback.h): badges, chips, status, progress, toasts, key caps and the pad legend.
#include "launcher/ui/components/feedback.h"
#include <numbers>
#include "launcher/i18n.h"
#include "launcher/ui/components/format.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <imgui_internal.h> // the legend's window, for the pad walk
#include <cmath>
#include <fmt/format.h>

namespace ui
{
    void DrawPortalMark(ImDrawList* draw, ImVec2 centre, float size, const PortalLook& look, float alpha, float lit)
    {
        // Flat shapes on a 40-unit grid, one dark line around them: no glow, no gradient.
        const float u = size / 40;
        const ImVec2 o(Snap(centre.x - size / 2), Snap(centre.y - size / 2));
        auto at = [&](float x, float y) { return ImVec2(o.x + x * u, o.y + y * u); };
        auto r = [&](float x, float y) { return ImVec2(x * u, y * u); };
        const float line = std::max(1.0f, size / 22), thin = std::max(1.0f, size / 40);
        const ImU32 ink = Col(color::Inset, alpha);
        auto col = [&](unsigned rgb) { return Col(rgb, alpha); };
        constexpr float Pi = std::numbers::pi_v<float>;
        // Off, its light is a darker shade of itself; on, it shines, with a flat halo in two steps around the lit part.
        // Only the brightness moves between the two (Lift keeps the hue), so a fade never passes through another colour.
        const float glow = std::clamp(lit, 0.0f, 1.0f);
        const unsigned light = Lift(look.light, -0.4f + 0.6f * glow);
        auto halo = [&](float y, float rx, float ry) {
            if (glow <= 0)
                return;
            draw->AddEllipseFilled(at(20, y), r(rx + 4, ry + 3.5f), Col(look.light, 0.18f * glow * alpha));
            draw->AddEllipseFilled(at(20, y), r(rx + 2, ry + 1.8f), Col(look.light, 0.3f * glow * alpha));
        };
        auto arc = [&](ImVec2 c, ImVec2 radius, float from, float to, ImU32 colour, float width) {
            draw->PathEllipticalArcTo(c, radius, 0, from, to);
            draw->PathStroke(colour, 0, width);
        };
        // A point on an ellipse, at `angle` (0 right, Pi / 2 the front).
        auto on = [&](float cx, float cy, float rx, float ry, float angle) {
            return at(cx + rx * std::cos(angle), cy + ry * std::sin(angle));
        };
        switch (look.shape)
        {
        case PortalShape::Stone:
        {
            // The base band under the wall, the wall, the rim of blocks, the top.
            const unsigned wall = Lift(look.body, -0.3f);
            halo(14, 18, 7);
            draw->AddEllipseFilled(at(20, 27), r(18.5f, 7), col(look.trim));
            arc(at(20, 27), r(18.5f, 7), 0, Pi, ink, line);
            draw->AddEllipseFilled(at(20, 24), r(18, 7), col(wall));
            draw->AddRectFilled(at(2, 14), at(38, 24), col(wall));
            draw->AddEllipseFilled(at(20, 14), r(18, 7), col(look.body));
            draw->AddEllipseFilled(at(20, 14), r(12.5f, 4.5f), col(light));
            for (float deg : { 35.0f, 62.0f, 118.0f, 145.0f }) // the wall's blocks
            {
                const float a = deg * Pi / 180;
                const ImVec2 top = on(20, 14, 18, 7, a);
                draw->AddLine(top, ImVec2(top.x, top.y + 10 * u), ink, thin);
            }
            for (float deg : { 0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f }) // the rim's blocks
            {
                const float a = deg * Pi / 180;
                draw->AddLine(on(20, 14, 12.5f, 4.5f, a), on(20, 14, 18, 7, a), ink, thin);
            }
            // The glyph on the front block, in the top's light.
            const ImVec2 glyph[4] = { at(20, 23.5f), at(21.6f, 26.5f), at(20, 29.5f), at(18.4f, 26.5f) };
            draw->AddConvexPolyFilled(glyph, 4, col(light));
            draw->AddEllipse(at(20, 14), r(12.5f, 4.5f), ink, 0, 0, thin);
            arc(at(20, 14), r(18, 7), Pi, 2 * Pi, ink, line);
            draw->AddLine(at(2, 14), at(2, 24), ink, line);
            draw->AddLine(at(38, 14), at(38, 24), ink, line);
            arc(at(20, 24), r(18, 7), 0, Pi, ink, line);
            break;
        }
        case PortalShape::Trap:
        {
            // The dark base, the clear ring and its opening, the trap slot at the front with its grille and lamp.
            const unsigned ring = Lift(look.body, -0.18f);
            halo(15, 18, 7);
            draw->AddEllipseFilled(at(20, 26), r(17, 6.5f), col(0x22252B));
            draw->AddEllipseFilled(at(20, 21), r(18, 7), col(ring));
            draw->AddRectFilled(at(2, 15), at(38, 21), col(ring));
            draw->AddEllipseFilled(at(20, 15), r(18, 7), col(look.body));
            draw->AddEllipseFilled(at(20, 15), r(13, 4.6f), col(Lift(look.trim, -0.75f + 0.45f * glow)));
            draw->AddEllipse(at(20, 15), r(13, 4.6f), ink, 0, 0, thin);
            arc(at(20, 15), r(18, 7), Pi, 2 * Pi, ink, line);
            draw->AddLine(at(2, 15), at(2, 21), ink, line);
            draw->AddLine(at(38, 15), at(38, 21), ink, line);
            arc(at(20, 21), r(18, 7), 0, Pi, ink, line);
            const ImVec2 slot[5] = { at(13.5f, 18), at(26.5f, 18), at(27.5f, 30), at(20, 35.5f), at(12.5f, 30) };
            draw->AddConvexPolyFilled(slot, 5, col(light));
            for (float y : { 24.0f, 27.0f, 30.0f })
                draw->AddLine(at(16, y), at(24, y), ink, thin * 1.5f);
            draw->AddCircleFilled(at(20, 21), 1.6f * u, col(Lift(look.trim, 0.35f * glow)));
            draw->AddPolyline(slot, 5, ink, ImDrawFlags_Closed, line);
            break;
        }
        case PortalShape::Engine:
        case PortalShape::Crystal:
        {
            // The pad: its edge, its rim, its dark top; then at the back the engine and its flames, or the crystal.
            halo(24.5f, 18, 8.5f);
            draw->AddEllipseFilled(at(20, 27), r(18, 8.5f), col(Lift(look.trim, -0.4f)));
            draw->AddEllipseFilled(at(20, 24.5f), r(18, 8.5f), col(Lift(look.trim, 0.35f * glow)));
            draw->AddEllipseFilled(at(20, 24.5f), r(15.5f, 6.8f), col(look.body));
            arc(at(20, 27), r(18, 8.5f), 0, Pi, ink, line);
            arc(at(20, 24.5f), r(18, 8.5f), Pi, 2 * Pi, ink, line);
            draw->AddLine(at(2, 24.5f), at(2, 27), ink, line);
            draw->AddLine(at(38, 24.5f), at(38, 27), ink, line);
            if (look.shape == PortalShape::Engine)
            {
                for (float side : { -1.0f, 1.0f })
                {
                    const ImVec2 flame[3] = { at(20 + side * 7, 9), at(20 + side * 15, 15), at(20 + side * 7, 17) };
                    draw->AddTriangleFilled(flame[0], flame[1], flame[2], col(light));
                    draw->AddPolyline(flame, 3, ink, ImDrawFlags_Closed, thin);
                }
                draw->AddRectFilled(at(13, 6), at(27, 19), col(0xC9D0D8), 2 * u);
                draw->AddRectFilled(at(13, 6), at(27, 9), col(0xEEF1F4), 2 * u, ImDrawFlags_RoundCornersTop);
                for (float x : { 17.0f, 20.0f, 23.0f })
                    draw->AddLine(at(x, 11), at(x, 17), ink, thin);
                draw->AddRect(at(13, 6), at(27, 19), ink, 2 * u, 0, line);
            }
            else
            {
                const ImVec2 crystal[6] = { at(20, 2), at(26, 8), at(26, 17), at(20, 23), at(14, 17), at(14, 8) };
                draw->AddConvexPolyFilled(crystal, 6, col(light));
                const ImVec2 shade[4] = { at(20, 2), at(20, 23), at(14, 17), at(14, 8) };
                draw->AddConvexPolyFilled(shade, 4, col(Lift(look.light, -0.3f)));
                draw->AddLine(at(20, 2), at(20, 23), ink, thin);
                draw->AddPolyline(crystal, 6, ink, ImDrawFlags_Closed, line);
            }
            break;
        }
        }
    }

    void DrawEmptySeat(ImDrawList* draw, ImVec2 at, float size)
    {
        const ImVec2 centre(at.x + size / 2, at.y + size / 2);
        const float r = size / 2 - stroke::Edge;
        for (int i = 0; i < 20; i++)
        {
            const float angle = float(i) * 2 * std::numbers::pi_v<float> / 20;
            draw->PathArcTo(centre, r, angle, angle + std::numbers::pi_v<float> / 20);
            draw->PathStroke(Col(color::Edge), 0, stroke::Edge);
        }
        DrawIcon(draw, Icon::Plus, centre, std::min(float(icon::Normal), size / 2), Col(color::Ink));
    }
    namespace
    {
        // Light or dark ink, whichever reads better on `fill`.
        unsigned BestInk(unsigned fill)
        {
            return Contrast(0xFFFFFF, fill) >= Contrast(color::OnAccent, fill) ? 0xFFFFFF : color::OnAccent;
        }
        bool VeryDark(unsigned rgb) { return Luminance(rgb) < 0.03f; }
    }

    // ---- Badge -----------------------------------------------------------------------------------------------------

    ImVec2 BadgeSize(const char* text)
    {
        ImVec2 size = TextSize(TextStyle::Caption, text, nullptr, true);
        return ImVec2(std::ceil(size.x + 2 * space::XS), std::ceil(size.y + space::XXS));
    }

    ImVec2 DrawBadge(ImDrawList* draw, ImVec2 at, const char* text, unsigned fill, unsigned ink)
    {
        const ImVec2 size = BadgeSize(text), end(at.x + size.x, at.y + size.y);
        draw->AddRectFilled(at, end, Col(fill), size.y / 2);
        if (VeryDark(fill)) // a very dark badge (Dark) gets a light edge on the dark cards
            draw->AddRect(at, end, Col(color::Dim), size.y / 2);
        PushText(TextStyle::Caption, true);
        DrawText(draw, ImVec2(at.x + space::XS, at.y + space::XXS / 2), Col(ink != 0 ? ink : BestInk(fill)), text);
        PopText();
        return size;
    }

    void Badge(const char* text, unsigned fill, unsigned ink)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(DrawBadge(ImGui::GetWindowDrawList(), at, text, fill, ink));
    }

    // ---- Chip ------------------------------------------------------------------------------------------------------

    bool Chip(const char* label, const ChipOptions& options)
    {
        const bool sort = options.kind == ChipKind::Sort, on = !sort && options.selected;
        const char* end = LabelEnd(label);
        const float h = height::Small, icon = icon::Small;
        const bool lead = on || sort;
        const float textW = TextSize(TextStyle::Body, label, end, on).x;
        const float w = std::ceil(textW + 2 * space::S + (lead ? icon + space::XS : 0));
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        const bool pressed = Hit(label, ImVec2(w, h));
        State state = ItemState(options.disabled);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float rounding = sort ? radius::Control : h / 2; // a filter is round, the sort is square-cornered
        const bool lit = state.hovered || state.pressed;
        unsigned ink = color::Ink;
        if (sort)
        {
            draw->AddRectFilled(min, max, Col(lit ? color::InsetHover : color::Inset), rounding);
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Edge),
                rounding, 0, stroke::Hairline);
        }
        else if (on)
        {
            unsigned fill = options.color != 0 ? options.color : Ctx().accent;
            draw->AddRectFilled(min, max, Col(lit ? Lift(fill, 0.12f) : fill), rounding);
            ink = BestInk(fill);
        }
        else
        {
            draw->AddRectFilled(min, max, Col(lit ? color::RaisedHover : color::Raised, 0.92f), rounding);
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Hairline),
                rounding, 0, stroke::Hairline);
        }
        float x = min.x + space::S;
        const float mid = std::round((min.y + max.y) / 2) + (state.pressed ? 1.0f : 0.0f);
        if (lead)
        {
            DrawIcon(draw, sort ? (options.direction < 0 ? Icon::ArrowDown : Icon::ArrowUp) : Icon::Check,
                ImVec2(x + icon / 2, mid), icon, Col(ink));
            x += icon + space::XS;
        }
        PushText(TextStyle::Body, on);
        DrawText(draw, ImVec2(x, std::round(mid - CapMiddle(TextStyle::Body, on))), Col(ink), label, end);
        PopText();
        ImGui::EndDisabled();
        FocusRingIf(state, rounding);
        return pressed && !options.disabled;
    }

    // ---- Status ----------------------------------------------------------------------------------------------------

    unsigned ColorOf(Status status)
    {
        switch (status)
        {
        case Status::Info:
        case Status::Busy: return color::Info;
        case Status::Success: return color::Success;
        case Status::Warning: return color::Warning;
        case Status::Error: return color::Danger;
        default: return color::Dim;
        }
    }

    Icon IconOf(Status status)
    {
        switch (status)
        {
        case Status::Info: return Icon::Info;
        case Status::Success: return Icon::Success;
        case Status::Warning: return Icon::Warning;
        case Status::Error: return Icon::Error;
        case Status::Busy: return Icon::Loader;
        default: return Icon::Unknown;
        }
    }

    void DrawStatusIcon(ImDrawList* draw, Status status, ImVec2 centre, float size)
    {
        DrawStatusIcon(draw, status, IconOf(status), centre, size);
    }

    void DrawStatusIcon(ImDrawList* draw, Status status, Icon icon, ImVec2 centre, float size)
    {
        if (status == Status::Busy)
            DrawSpinner(draw, centre, size, Col(ColorOf(status)));
        else
            DrawIcon(draw, icon, centre, size, Col(ColorOf(status)));
    }

    void StatusPill(Status status, const char* text, Icon icon)
    {
        // [XS] icon [XXS] text [XS + 2]: the icon's own margin (Lucide's 2 of 24) evens the ends.
        const float iconSize = icon::Small, h = PillHeight;
        const float textW = TextSize(TextStyle::Caption, text, nullptr, true).x;
        const ImVec2 min = ImGui::GetCursorScreenPos(),
                     max(min.x + std::ceil(space::XS + iconSize + space::XXS + textW + space::XS + 2), min.y + h);
        ImGui::Dummy(ImVec2(max.x - min.x, h));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const unsigned tone = ColorOf(status);
        const float mid = min.y + h / 2;
        draw->AddRectFilled(min, max, Col(Mix(color::Surface, tone, 0.16f)), h / 2);
        draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(tone, 0.5f), h / 2, 0,
            stroke::Hairline);
        DrawStatusIcon(draw, status, icon != Icon::None ? icon : IconOf(status),
            ImVec2(min.x + space::XS + iconSize / 2, mid), iconSize);
        PushText(TextStyle::Caption, true);
        DrawText(draw,
            ImVec2(min.x + space::XS + iconSize + space::XXS, std::round(mid - CapMiddle(TextStyle::Caption, true))),
            Col(color::Ink), text);
        PopText();
    }

    void StatusLine(Status status, const char* text, float width, Icon stateIcon)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        DrawStatusIcon(ImGui::GetWindowDrawList(), status, stateIcon != Icon::None ? stateIcon : IconOf(status),
            ImVec2(at.x + icon::Small / 2, at.y + CapMiddle(TextStyle::Body)), icon::Small);
        ImGui::SetCursorScreenPos(ImVec2(at.x + icon::Small + space::XS, at.y));
        PushText(TextStyle::Body);
        const std::string shown = width > 0 ? Ellipsize(text, width - icon::Small - space::XS) : text;
        PopText();
        Text(TextStyle::Body, shown.c_str(), color::InkSoft, -1);
        if (shown != text && ImGui::IsItemHovered())
            Bubble(text, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    // ---- Progress --------------------------------------------------------------------------------------------------

    namespace
    {
        constexpr float BarH = space::XS;

        // The track; returns its rectangle.
        void Track(ImVec2 min, ImVec2 max)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                min, max, Col(color::Dim, 0.35f), BarH / 2); // light: the bar's end shows on dark art
        }
    }

    void ProgressBar(float fraction, float width, bool showPercent)
    {
        const std::string percent = FormatPercent(fraction);
        const float label = showPercent ? TextSize(TextStyle::Caption, FormatPercent(1.0f).c_str()).x + space::S : 0;
        const float w = (width < 0 ? ImGui::GetContentRegionAvail().x : width) - label;
        const float h = showPercent ? type::Caption * 1.3f : BarH;
        const ImVec2 at = ImGui::GetCursorScreenPos(), min(at.x, std::round(at.y + (h - BarH) / 2)),
                     max(at.x + w, min.y + BarH);
        ImGui::Dummy(ImVec2(w + label, h));
        Track(min, max);
        if (fraction > 0) // a whole cap at both ends, even when nearly empty
            ImGui::GetWindowDrawList()->AddRectFilled(min,
                ImVec2(min.x + std::max(w * std::min(fraction, 1.0f), BarH), max.y), Col(color::AccentFill), BarH / 2);
        if (showPercent)
        {
            PushText(TextStyle::Caption);
            DrawText(ImGui::GetWindowDrawList(), ImVec2(at.x + w + space::S, at.y + (h - ImGui::GetFontSize()) / 2),
                Col(color::InkSoft), percent.c_str());
            PopText();
        }
    }

    void IndeterminateBar(float width)
    {
        const float w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + BarH);
        ImGui::Dummy(ImVec2(w, BarH));
        Track(min, max);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (Ctx().reduceMotion)
        {
            // Still: short dashes along the whole track, which no one reads as a percentage.
            for (float x = min.x; x + BarH < max.x; x += 3 * BarH)
                draw->AddRectFilled(
                    ImVec2(x, min.y), ImVec2(x + 1.5f * BarH, max.y), Col(color::AccentFill, 0.7f), BarH / 2);
            return;
        }
        const float band = w * 0.3f, from = min.x - band + (w + band) * Phase(motion::Spin * 2);
        draw->PushClipRect(min, max, true);
        draw->AddRectFilled(ImVec2(from, min.y), ImVec2(from + band, max.y), Col(color::AccentFill), BarH / 2);
        draw->PopClipRect();
    }

    void Spinner(float size)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size, size));
        DrawSpinner(ImGui::GetWindowDrawList(), ImVec2(at.x + size / 2, at.y + size / 2), size, Col(color::Info));
    }

    // ---- Toasts ----------------------------------------------------------------------------------------------------

    void Toasts::Push(std::string text, ToastKind kind)
    {
        // Long enough to read: 5 s, more for a long message, up to 8 s. An error stays until it is closed.
        const uint64_t now = SDL_GetTicks();
        const uint64_t readMs = std::clamp<uint64_t>(5000 + 45 * (text.size() > 40 ? text.size() - 40 : 0), 5000, 8000);
        const uint64_t until = kind == ToastKind::Error ? 0 : now + readMs;
        for (Item& item : m_items)
            if (item.text == text) // the same words again: the one already shown, kept longer
            {
                if (kind == ToastKind::Error || item.kind == ToastKind::Error)
                {
                    item.kind = ToastKind::Error;
                    item.until = 0;
                }
                else
                    item.until = until;
                return;
            }
        m_items.push_back({ std::move(text), kind, until, ++m_serial });
        if (m_items.size() > Max)
            m_items.erase(m_items.begin());
    }

    int Toasts::NextWakeMs(uint64_t now) const
    {
        int wait = -1;
        for (const Item& item : m_items)
            if (item.until > now)
                wait = wait < 0 ? int(item.until - now) + 1 : std::min(wait, int(item.until - now) + 1);
        return wait;
    }

    bool Toasts::Draw(float bottomInset, float maxTextWidth)
    {
        const uint64_t now = SDL_GetTicks();
        std::erase_if(m_items, [&](const Item& item) { return item.until != 0 && now > item.until; });
        m_focused = false;
        if (m_items.empty())
            return false;
        // Bottom right, stacked upwards, newest at the bottom; each at most `maxTextWidth` of text (a long one wraps).
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const bool navigating = ImGui::GetIO().NavVisible;
        const float icon = icon::Normal, padX = space::M, padY = space::S, cross = height::Small;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        float bottom = display.y - space::L - bottomInset;
        int closed = -1;
        bool closedByNav = false;
        // The newest error takes the keyboard and pad focus once, when they are in use.
        const Item* newestError = nullptr;
        for (const Item& item : m_items)
            if (item.kind == ToastKind::Error)
                newestError = &item;
        PushText(TextStyle::Body);
        for (size_t i = m_items.size(); i-- > 0;)
        {
            const Item& item = m_items[i];
            const bool error = item.kind == ToastKind::Error;
            const unsigned tone = error                             ? color::Danger
                                  : item.kind == ToastKind::Success ? color::Success
                                                                    : color::Info;
            const ImVec2 text = ImGui::CalcTextSize(item.text.c_str(), nullptr, false, maxTextWidth);
            const float width = padX + icon + space::S + text.x + padX + (error ? cross : 0.0f);
            const float heightNeeded = std::max(text.y, error ? cross : 0.0f) + 2 * padY;
            // Whole points, so the ring covers whole pixels at 1x and 2x.
            const ImVec2 min(std::round(display.x - space::L - width), std::round(bottom - heightNeeded)),
                max(std::round(display.x - space::L), std::round(bottom));
            // A window of its own, so a click closes the message and does not reach what is under it.
            ImGui::SetNextWindowPos(min);
            ImGui::SetNextWindowSize(ImVec2(max.x - min.x, max.y - min.y));
            if (error && navigating && &item == newestError && m_focusedSerial != item.serial)
            {
                ImGui::SetNextWindowFocus();
                m_focusedSerial = item.serial;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                           (error ? 0 : ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
            ImGui::Begin(fmt::format("##toast{}", i).c_str(), nullptr, flags);
            if (Hit("##close", ImVec2(max.x - min.x, max.y - min.y)))
            {
                closed = int(i);
                closedByNav = navigating && !ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            }
            const bool hovered = ImGui::IsItemHovered();
            const bool focused =
                error && ((ImGui::IsWindowFocused() && navigating) || (m_previewFocus && &item == newestError));
            if (focused)
            {
                m_focused = true;
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
                {
                    closed = int(i);
                    closedByNav = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            draw->AddRectFilled(
                min, max, LauncherTheme::Rgb(elevation::Raised.fill, elevation::Raised.fillAlpha), radius::Card);
            // AddRect puts the line's centre half a point in: a 2-point ring then runs between whole points only from 1 point in.
            draw->AddRect(ImVec2(min.x + 1, min.y + 1), ImVec2(max.x - 1, max.y - 1), LauncherTheme::Rgb(tone, 200),
                radius::Card - 1, 0, 2.0f);
            const float mid = (min.y + max.y) / 2, textTop = std::round(mid - text.y / 2);
            DrawIcon(draw,
                error                             ? Icon::Error
                : item.kind == ToastKind::Success ? Icon::Check
                                                  : Icon::Info,
                ImVec2(min.x + padX + icon / 2, std::round(textTop + ImGui::GetFontSize() / 2)), icon,
                LauncherTheme::Rgb(tone));
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + padX + icon + space::S, textTop),
                LauncherTheme::Rgb(color::Ink), item.text.c_str(), nullptr, maxTextWidth);
            if (error) // the cross, or with the keyboard / pad focus the key that closes it
            {
                const ImVec2 centre(max.x - padX / 2 - cross / 2, mid);
                if (focused)
                    DrawKeyCap(draw, ImVec2(centre.x - 12, centre.y), PadButton::South,
                        Ctx().glyphs == Glyphs::Keyboard ? Glyphs::Xbox : Ctx().glyphs);
                else
                    DrawIcon(
                        draw, Icon::Cross, centre, icon::Small, LauncherTheme::Rgb(hovered ? color::Ink : color::Dim));
            }
            if (focused)
            {
                const float out = focus::Offset + focus::Width / 2;
                draw->AddRect(ImVec2(min.x - out, min.y - out), ImVec2(max.x + out, max.y + out),
                    LauncherTheme::Rgb(Ctx().accent), radius::Card + out, 0, focus::Width);
            }
            bottom = min.y - space::XS;
        }
        PopText();
        if (closed >= 0)
            m_items.erase(m_items.begin() + closed);
        return closedByNav;
    }

    // ---- Key caps and the pad legend -------------------------------------------------------------------------------

    const char* PadButtonLabel(PadButton button, Glyphs glyphs)
    {
        static const char* const Xbox[] = { "A", "B", "X", "Y", "LB", "RB", "Menu", "View", "LT", "RT" };
        static const char* const PlayStation[] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Options",
            "Share", "L2", "R2" };
        static const char* const Nintendo[] = { "B", "A", "Y", "X", "L", "R", "+", "-", "ZL", "ZR" };
        const int i = int(button);
        switch (glyphs)
        {
        case Glyphs::PlayStation: return PlayStation[i];
        case Glyphs::Nintendo: return Nintendo[i];
        case Glyphs::Xbox: return Xbox[i];
        default: return "";
        }
    }

    namespace
    {
        constexpr float CapH = type::Caption + space::XS + 2; // a key cap's height

        float CapWidth(const char* text, bool round)
        {
            if (round)
                return CapH;
            return std::max(CapH, std::ceil(TextSize(TextStyle::Caption, text, nullptr, true).x + 2 * space::XS));
        }

        ImVec2 DrawCapBox(ImDrawList* draw, ImVec2 at, const char* text, bool round)
        {
            PushText(TextStyle::Caption, true);
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const float w = CapWidth(text, round);
            const ImVec2 min(at.x, std::round(at.y - CapH / 2)), max(min.x + w, min.y + CapH);
            const float rounding = round ? CapH / 2 : radius::Control * 0.6f;
            draw->AddRectFilled(
                ImVec2(min.x, min.y + 2), ImVec2(max.x, max.y + 2), Col(0x000000, 0.35f), rounding); // its depth
            draw->AddRectFilled(min, max, Col(color::Raised), rounding);
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(color::Edge),
                rounding, 0, stroke::Hairline);
            if (text[0] != 0)
                DrawText(draw,
                    ImVec2(std::round((min.x + max.x - textSize.x) / 2), std::round((min.y + max.y - textSize.y) / 2)),
                    Col(color::Ink), text);
            PopText();
            return ImVec2(w, CapH);
        }
    }

    ImVec2 DrawKeyCap(ImDrawList* draw, ImVec2 at, PadButton button, Glyphs glyphs)
    {
        const bool face = button <= PadButton::North;
        if (!face || glyphs != Glyphs::PlayStation)
            return DrawCapBox(draw, at, PadButtonLabel(button, glyphs), face);
        // PlayStation's face buttons are shapes: Lucide's, in the cap.
        ImVec2 size = DrawCapBox(draw, at, "", true);
        static const Icon Shapes[] = { Icon::Cross, Icon::Circle, Icon::Square, Icon::Triangle };
        DrawIcon(draw, Shapes[int(button)], ImVec2(at.x + CapH / 2, std::round(at.y - CapH / 2) + CapH / 2),
            icon::Small, Col(color::Ink));
        return size;
    }

    ImVec2 DrawKeyCap(ImDrawList* draw, ImVec2 at, const char* key) { return DrawCapBox(draw, at, key, false); }

    float KeyCapWidth(PadButton button, Glyphs glyphs)
    {
        return CapWidth(PadButtonLabel(button, glyphs), button <= PadButton::North);
    }
    float KeyCapWidth(const char* key) { return CapWidth(key, false); }
    float KeyCapHeight() { return CapH; }

    void KeyCap(PadButton button, Glyphs glyphs)
    {
        SettingControl(KeyCapWidth(button, glyphs));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(DrawKeyCap(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + CapH / 2), button, glyphs));
    }

    void KeyCap(const char* key)
    {
        SettingControl(KeyCapWidth(key));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(DrawKeyCap(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + CapH / 2), key));
    }

    void PadLegend(std::span<const PadHint> hints, Glyphs glyphs, float width)
    {
        const float h = height::Normal, w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
        if (Ctx().testLegends != nullptr)
        {
            const uint32_t root = ImGui::GetCurrentWindow()->RootWindow->ID;
            Ctx().testLegends->push_back(root);
            auto& entries = Ctx().testLegendHints[root];
            entries.clear();
            for (const PadHint& hint : hints)
                entries.push_back({ int(hint.button), hint.label, hint.withNext });
        }
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, Col(color::Surface, 0.92f));
        draw->AddLine(ImVec2(min.x, min.y + 0.5f), ImVec2(max.x, min.y + 0.5f), Col(color::Hairline), stroke::Hairline);
        // Right-aligned, in the order given: measured first.
        PushText(TextStyle::Body);
        const bool keyboard = glyphs == Glyphs::Keyboard;
        float total = 0;
        int shown = 0;
        for (const PadHint& hint : hints)
        {
            if (keyboard && hint.key == nullptr)
                continue;
            float cap = keyboard ? KeyCapWidth(hint.key) : KeyCapWidth(hint.button, glyphs);
            if (hint.withNext && !keyboard)
                cap += space::XXS + KeyCapWidth(PadButton(int(hint.button) + 1), glyphs);
            total += cap + space::XS + ImGui::CalcTextSize(hint.label).x;
            shown++;
        }
        // L between hints; M, then S when a long legend (French, a narrow window) would run past the band's left edge.
        float gap = space::L;
        for (float tighter : { space::M, space::S })
            if (total + gap * float(std::max(0, shown - 1)) > w - 2 * space::M)
                gap = tighter;
        float x = max.x - space::M - (total + gap * float(std::max(0, shown - 1)));
        const float mid = std::round((min.y + max.y) / 2);
        for (const PadHint& hint : hints)
        {
            if (keyboard && hint.key == nullptr)
                continue;
            float cap = keyboard ? DrawKeyCap(draw, ImVec2(x, mid), hint.key).x
                                 : DrawKeyCap(draw, ImVec2(x, mid), hint.button, glyphs).x;
            if (hint.withNext && !keyboard)
                cap += space::XXS +
                       DrawKeyCap(draw, ImVec2(x + cap + space::XXS, mid), PadButton(int(hint.button) + 1), glyphs).x;
            x += cap + space::XS;
            DrawText(draw, ImVec2(x, mid - ImGui::GetFontSize() / 2), Col(color::InkSoft), hint.label);
            x += ImGui::CalcTextSize(hint.label).x + gap;
        }
        PopText();
    }
}
