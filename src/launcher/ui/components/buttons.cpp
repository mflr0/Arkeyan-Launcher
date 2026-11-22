// Buttons (buttons.h): Button, Pill, IconButton, MainAction.
#include "launcher/ui/components/buttons.h"
#include "launcher/i18n.h"
#include "launcher/ui/components/format.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <string>

namespace ui
{
    namespace
    {
        // How a button of `kind` looks in `state`: its fill (alpha 0: none), its edge and its ink.
        struct Paint
        {
            unsigned fill = 0;
            float fillAlpha = 1;
            unsigned edge = 0;
            float edgeWidth = 0;
            unsigned ink = color::Ink;
        };

        Paint PaintFor(ButtonKind kind, const State& state, bool overArt, bool selected = false)
        {
            Paint paint;
            const bool down = state.pressed, over = state.hovered;
            if (selected || kind == ButtonKind::Primary)
            {
                paint.fill = down ? color::AccentFillPressed : over ? color::AccentFillHover : color::AccentFill;
                paint.ink = color::OnAccent;
                return paint;
            }
            switch (kind)
            {
            case ButtonKind::Danger:
                paint.fill = down ? color::DangerFillPressed : over ? color::DangerFillHover : color::DangerFill;
                paint.edge = color::Danger;
                paint.edgeWidth = 1.5f;
                break;
            case ButtonKind::Ghost:
                paint.fill = overArt ? (down ? color::OverArtPressed : color::OverArtHover)
                                     : (down ? color::RaisedPressed : color::RaisedHover);
                paint.fillAlpha = down || over ? 1.0f : 0.0f;
                paint.ink = down || over ? color::Ink : color::InkSoft;
                break;
            default:
                if (overArt)
                    paint.fill = down ? color::OverArtPressed : over ? color::OverArtHover : color::OverArt;
                else
                    paint.fill = down ? color::RaisedPressed : over ? color::RaisedHover : color::Raised;
                paint.edge = overArt ? color::OverArtEdge : color::Hairline;
                paint.edgeWidth = 1;
                break;
            }
            return paint;
        }

        void DrawFace(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, const Paint& paint)
        {
            if (paint.fillAlpha > 0)
                draw->AddRectFilled(min, max, Col(paint.fill, paint.fillAlpha), rounding);
            if (paint.edgeWidth > 0) // inside the face: the half-point offset keeps a 1-point line on whole pixels
                draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), Col(paint.edge),
                    rounding, 0, paint.edgeWidth);
        }

        constexpr float IconSize = icon::Normal;    // an icon beside a label
        constexpr const char* PrefixDot = "\xC2\xB7"; // between a pill's prefix and its icon
        constexpr float TrailingSize = icon::Small; // the chevron after a label

        // ChevronDown, ChevronUp while open: the button opens a menu or a list, and its chevron is a Select's.
        bool OpensMenu(Icon trailing) { return trailing == Icon::ChevronDown || trailing == Icon::ChevronUp; }

        float TrailingGap(Icon trailing) { return OpensMenu(trailing) ? space::XS : space::XXS; }

        float TrailingWidth(Icon trailing) { return trailing == Icon::None ? 0 : TrailingGap(trailing) + TrailingSize; }

        unsigned TrailingInk(Icon trailing, unsigned ink) // a filled button's chevron keeps the fill's ink
        {
            if (!OpensMenu(trailing) || ink == color::OnAccent)
                return ink;
            return trailing == Icon::ChevronUp ? color::Ink : color::InkSoft;
        }

        // The content of a button: [prefix] [icon] label [trailing], centred in min..max, one point lower when pressed.
        void DrawContent(ImDrawList* draw, ImVec2 min, ImVec2 max, const char* label, TextStyle style, Icon icon,
            bool spinner, Icon trailing, unsigned ink, bool pressed, const char* prefix = nullptr,
            unsigned iconInk = 0) // iconInk: an rgb, 0 the ink
        {
            const char* end = LabelEnd(label);
            PushText(style, true);
            ImVec2 text = ImGui::CalcTextSize(label, end);
            const float prefixW = prefix != nullptr
                                      ? ImGui::CalcTextSize(prefix).x + ImGui::CalcTextSize(PrefixDot).x + 2 * space::XS
                                      : 0;
            bool hasText = end != label, lead = icon != Icon::None || spinner;
            float width =
                prefixW + text.x + (lead ? IconSize + (hasText ? space::XS : 0) : 0) + TrailingWidth(trailing);
            float x = std::round((min.x + max.x - width) / 2),
                  mid = std::round((min.y + max.y) / 2) + (pressed ? 1.0f : 0.0f);
            if (prefix != nullptr) // the words, then a dot XS apart from them and from the icon
            {
                const float y = std::round(mid - CapMiddle(style, true));
                DrawText(draw, ImVec2(x, y), Col(ink), prefix);
                DrawText(draw, ImVec2(x + ImGui::CalcTextSize(prefix).x + space::XS, y), Col(ink), PrefixDot);
                x += prefixW;
            }
            if (spinner)
                DrawSpinner(draw, ImVec2(x + IconSize / 2, mid), IconSize, Col(ink));
            else if (icon != Icon::None)
                DrawIcon(draw, icon, ImVec2(x + IconSize / 2, mid), IconSize, Col(iconInk != 0 ? iconInk : ink));
            if (lead)
                x += IconSize + (hasText ? space::XS : 0);
            if (hasText) // the capitals' middle on the button's: the icons beside the label line up with it
                DrawText(draw, ImVec2(x, std::round(mid - CapMiddle(style, true))), Col(ink), label, end);
            if (trailing != Icon::None)
                DrawIcon(draw, trailing, ImVec2(x + text.x + TrailingGap(trailing) + TrailingSize / 2, mid),
                    TrailingSize, Col(TrailingInk(trailing, ink)));
            PopText();
        }

        TextStyle LabelStyle(Size size) { return size == Size::Large ? TextStyle::Label : TextStyle::Body; }
        float SidePad(Size size) { return size == Size::Small ? space::M : space::L; }
    }

    float HeightOf(Size size)
    {
        return size == Size::Small ? height::Small : size == Size::Large ? height::Large : height::Normal;
    }

    float ButtonWidth(const char* label, const ButtonOptions& options)
    {
        if (options.width < 0)
            return ImGui::GetContentRegionAvail().x;
        const char* end = LabelEnd(label);
        float text = TextSize(LabelStyle(options.size), label, end, true).x;
        bool lead = options.icon != Icon::None || options.loading;
        float content = text + (lead ? IconSize + (end != label ? space::XS : 0) : 0) + TrailingWidth(options.trailing);
        return std::max(options.width, std::ceil(content + 2 * SidePad(options.size)));
    }

    bool Button(const char* label, const ButtonOptions& options)
    {
        Context& context = Ctx();
        const float h = HeightOf(options.size), w = ButtonWidth(label, options), rounding = radius::Control;
        SettingControl(w);
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        bool clicked = Hit(label, ImVec2(w, h));
        State state = ItemState(options.disabled, options.loading);
        const ImGuiID id = ImGui::GetItemID();

        // Hold to confirm: held (pressed and, for the mouse, still over it) for HoldConfirm::HoldMs. Space, Enter and the
        // pad's A hold it too; leaving or releasing early cancels and shows the hint.
        float fill = 0;
        bool done = false;
        if (options.hold && !options.disabled && !options.loading)
        {
            bool mouse = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool held = ImGui::IsItemActive() &&
                        (!mouse || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
            bool inputDown = mouse || ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
                             ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
            if (context.testHeld != 0 && id == context.testHeld)
                held = inputDown = true;
            fill = context.hold.Update(id, held, inputDown, context.now, done);
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID firedKey = id ^ 0x484F4C44; // "HOLD"
            if (done)
                storage->SetBool(firedKey, true);
            if (held)
                context.animating = true;
            if (ImGui::IsItemDeactivated())
            {
                if (!storage->GetBool(firedKey)) // let go too soon: say how it works
                {
                    context.holdHintId = id;
                    context.holdHintUntil = context.now + motion::HoldHint;
                }
                storage->SetBool(firedKey, false);
            }
            state.pressed = state.pressed || held;
        }
        if (state.holdFill >= 0)
            fill = state.holdFill;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        Paint paint = PaintFor(options.kind, state, options.overArt);
        DrawFace(draw, min, max, rounding, paint);
        if (fill > 0) // the hold's sweep, rounded at its leading end like the face (at least two radii wide)
        {
            unsigned sweep = options.kind == ButtonKind::Danger    ? color::Danger
                             : options.kind == ButtonKind::Primary ? color::AccentFillPressed
                                                                   : context.accent;
            draw->AddRectFilled(min, ImVec2(min.x + std::max(w * std::min(fill, 1.0f), 2 * rounding), max.y),
                Col(sweep, 0.55f), rounding);
        }
        // A hold button shows what it does (a bin for a removal) when it says so; the timer only when it has no icon of its own.
        Icon icon = options.icon; // a hold button has none of its own: the sweep and the hint say how it works
        DrawContent(draw, min, max, label, LabelStyle(options.size), icon, options.loading, options.trailing, paint.ink,
            state.pressed);
        ImGui::EndDisabled();
        FocusRingIf(state, rounding);

        const char* hint = options.tooltip;
        if (options.hold && hint == nullptr)
            hint = Tr("Hold to confirm");
        bool tapped = options.hold && context.holdHintId == id && context.now < context.holdHintUntil;
        if (hint != nullptr && (WantsTooltip(state) || tapped) && fill <= 0)
            Bubble(hint, min, max);
        if (options.disabled || options.loading)
            return false;
        return options.hold ? done : clicked;
    }

    float PillWidth(const char* label, const PillOptions& options)
    {
        const char* end = LabelEnd(label);
        float text = TextSize(TextStyle::Body, label, end, true).x;
        float content =
            text + (options.icon != Icon::None ? IconSize + space::XS : 0) + TrailingWidth(options.trailing) +
            (options.prefix != nullptr ? TextSize(TextStyle::Body, options.prefix, nullptr, true).x +
                                             TextSize(TextStyle::Body, PrefixDot, nullptr, true).x + 2 * space::XS
                                       : 0);
        return std::max(options.width, std::ceil(content + 2 * space::M));
    }

    bool Pill(const char* label, const PillOptions& options)
    {
        const float h = height::Small, w = PillWidth(label, options);
        SettingControl(w);
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + w, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        bool clicked = Hit(label, ImVec2(w, h));
        State state = ItemState(options.disabled);
        Paint paint = PaintFor(ButtonKind::Secondary, state, options.overArt, options.selected);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        DrawFace(draw, min, max, h / 2, paint);
        DrawContent(draw, min, max, label, TextStyle::Body, options.icon, false, options.trailing, paint.ink,
            state.pressed, options.prefix, options.iconColor);
        ImGui::EndDisabled();
        FocusRingIf(state, h / 2);
        if (options.tooltip != nullptr && WantsTooltip(state))
            Bubble(options.tooltip, min, max);
        return clicked && !options.disabled;
    }

    namespace
    {
        constexpr float KeyDepth = 3; // a key cap's side, under its face

        const char* KeyShown(const char* key, const KeyButtonOptions& options)
        {
            return options.capturing ? "..." : key;
        }
        bool KeyWarns(const KeyButtonOptions& options) { return options.conflict && !options.capturing; }
    }

    float KeyButtonWidth(const char* key, const KeyButtonOptions& options)
    {
        const char* shown = KeyShown(key, options);
        const float text = TextSize(TextStyle::Body, shown, nullptr, true).x;
        return std::max(
            height::Normal, std::ceil(text + (KeyWarns(options) ? icon::Small + space::XXS : 0) + 2 * space::S));
    }

    bool KeyButton(const char* id, const char* key, const KeyButtonOptions& options)
    {
        const char* shown = KeyShown(key, options);
        const float w = KeyButtonWidth(key, options), h = height::Normal;
        SettingControl(w);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = Hit(id, ImVec2(w, h));
        const State state = ItemState();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // The face is the item (what lines up with a row); its side shows KeyDepth under it, and the face goes down onto it
        // while pressed.
        const float down = state.pressed ? KeyDepth - 1 : 0;
        const ImVec2 min(at.x, at.y + down), max(at.x + w, at.y + h + down);
        draw->AddRectFilled(ImVec2(at.x, at.y + KeyDepth), ImVec2(at.x + w, at.y + h + KeyDepth), Col(0x000000, 0.35f),
            radius::Control);
        const unsigned fill = options.capturing ? color::AccentFill
                              : state.pressed   ? color::RaisedPressed
                              : state.hovered   ? color::RaisedHover
                                                : color::Raised;
        draw->AddRectFilled(min, max, Col(fill), radius::Control);
        const bool warns = KeyWarns(options);
        if (warns || !options.capturing)
            draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
                Col(warns ? color::Warning : color::Edge), radius::Control, 0, warns ? stroke::Edge : stroke::Hairline);
        // [warning] name, centred on the face, the capitals on its middle.
        PushText(TextStyle::Body, true);
        const float textW = ImGui::CalcTextSize(shown).x, contentW = textW + (warns ? icon::Small + space::XXS : 0);
        float x = std::round((min.x + max.x - contentW) / 2);
        const float mid = std::round((min.y + max.y) / 2);
        if (warns)
        {
            DrawIcon(draw, Icon::Warning, ImVec2(x + icon::Small / 2, mid), icon::Small, Col(color::Warning));
            x += icon::Small + space::XXS;
        }
        DrawText(draw, ImVec2(x, std::round(mid - CapMiddle(TextStyle::Body, true))),
            Col(options.capturing ? color::OnAccent : color::Ink), shown);
        PopText();
        if (state.focused) // around the face and its side
            FocusRing(at, ImVec2(at.x + w, at.y + h + KeyDepth), radius::Control);
        if (options.tooltip != nullptr && WantsTooltip(state))
            Bubble(options.tooltip, at, ImVec2(at.x + w, at.y + h + KeyDepth));
        return clicked;
    }

    bool IconButton(const char* id, Icon icon, const char* tooltip, const IconButtonOptions& options)
    {
        const float h = HeightOf(options.size);
        SettingControl(h);
        const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + h, min.y + h);
        ImGui::BeginDisabled(options.disabled);
        bool clicked = Hit(id, ImVec2(h, h));
        State state = ItemState(options.disabled);
        Paint paint = PaintFor(options.kind, state, options.overArt, options.selected);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        DrawFace(draw, min, max, h / 2, paint);
        ImVec2 centre(std::round((min.x + max.x) / 2), std::round((min.y + max.y) / 2) + (state.pressed ? 1.0f : 0.0f));
        if (options.image.GetTexID() != ImTextureID_Invalid)
        {
            const float mark = std::round(h * 0.45f);
            draw->AddImage(options.image, ImVec2(centre.x - mark / 2, centre.y - mark / 2),
                ImVec2(centre.x + mark / 2, centre.y + mark / 2), ImVec2(0, 0), ImVec2(1, 1), Col(paint.ink));
        }
        else
            DrawIcon(draw, icon, centre, options.size == Size::Small ? icon::Small : icon::Normal, Col(paint.ink));
        ImGui::EndDisabled();
        FocusRingIf(state, h / 2);
        if (tooltip != nullptr && WantsTooltip(state))
            Bubble(tooltip, min, max);
        return clicked && !options.disabled;
    }

    // ---- MainAction ------------------------------------------------------------------------------------------------

    namespace
    {
        // The main action's face as drawn.
        struct Look
        {
            bool hovered = false, held = false, enabled = true;
        };

        // A fill in its state: lighter under the pointer (or the focus), darker pressed, greyed when disabled.
        unsigned Shade(unsigned rgb, const Look& look)
        {
            if (!look.enabled)
                return Mix(rgb, 0x4A4A54, 0.7f);
            return look.held ? Lift(rgb, -0.1f) : look.hovered ? Lift(rgb, 0.1f) : rgb;
        }

        // A busy button's progress on its face: the done part tinted in the game's colour, and a bar along the bottom edge,
        // both following the face's corners. Indeterminate: a band sweeping along that bar.
        void DrawProgress(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImDrawFlags flags, unsigned rgb,
            float progress, bool indeterminate)
        {
            constexpr float BarH = 5;
            draw->PushClipRect(ImVec2(min.x, max.y - BarH), max, true);
            draw->AddRectFilled(min, max, Col(0xFFFFFF, 0.11f), rounding, flags);
            draw->PopClipRect();
            float from = min.x, to = Snap(min.x + (max.x - min.x) * std::clamp(progress, 0.0f, 1.0f));
            if (indeterminate)
            {
                float t = Phase(motion::Spin * 2), band = (max.x - min.x) * 0.3f;
                from = min.x - band + (max.x - min.x + band) * t;
                to = from + band;
            }
            else
            {
                draw->PushClipRect(min, ImVec2(to, max.y), true);
                draw->AddRectFilled(min, max, Col(rgb, 0.24f), rounding, flags);
                draw->PopClipRect();
            }
            draw->PushClipRect(ImVec2(std::max(from, min.x), max.y - BarH), ImVec2(std::min(to, max.x), max.y), true);
            draw->AddRectFilled(min, max, Col(rgb), rounding, flags);
            draw->PopClipRect();
        }

        // The label in the current font, centred in min..max on its capitals, on whole pixels.
        void DrawCapsLabel(ImDrawList* draw, ImVec2 min, ImVec2 max, const char* text, ImU32 ink)
        {
            const ImFontGlyph* cap = ImGui::GetFontBaked()->FindGlyph('H');
            float width = ImGui::CalcTextSize(text).x;
            draw->AddText(
                ImVec2(Snap((min.x + max.x - width) / 2), Snap((min.y + max.y) / 2 - (cap->Y0 + cap->Y1) / 2)), ink,
                text);
        }

        // The running game's Stop: one quieter button in the danger colours, no gear.
        MainActionResult StopAction(const MainActionOptions& options)
        {
            MainActionResult result;
            const ImVec2 at = ImGui::GetCursorScreenPos(), end(at.x + options.width, at.y + height::Large);
            result.mainCentre = ImVec2((at.x + end.x) / 2, (at.y + end.y) / 2);
            result.gearCentre = ImVec2(-1, -1);
            ImGui::BeginDisabled(!options.enabled);
            if (Hit("##main", ImVec2(options.width, height::Large)))
                result.hit = MainActionHit::Main;
            State state = ItemState(!options.enabled);
            if (options.takeFocus && options.enabled)
            {
                FocusLastItem(options.showFocus);
                state.focused = options.showFocus;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const unsigned fill = state.pressed                    ? color::DangerFillPressed
                                  : state.hovered || state.focused ? color::DangerFillHover
                                                                   : color::DangerFill;
            draw->AddRectFilled(at, end, Col(fill, DisabledAlpha), radius::Control);
            draw->AddRect(at, end, Col(color::Danger), radius::Control, 0, stroke::Edge);
            const float lead = icon::Normal + space::S;
            const float size =
                FitSize(Ctx().fonts.title, type::Title, options.label, options.width - 2 * space::M - lead);
            ImGui::PushFont(Ctx().fonts.title, size);
            const float x = result.mainCentre.x - (lead + ImGui::CalcTextSize(options.label).x) / 2;
            const float mid = result.mainCentre.y + (state.pressed ? stroke::Hairline : 0);
            DrawIcon(draw, options.icon, ImVec2(x + icon::Normal / 2, mid), icon::Normal, Col(color::Danger));
            DrawText(draw, ImVec2(x + lead, mid - CapMiddle(TextStyle::Title, false, size)), Col(color::Danger),
                options.label);
            ImGui::PopFont();
            FocusRingIf(state, radius::Control);
            ImGui::EndDisabled();
            return result;
        }
    }

    MainActionResult MainAction(const MainActionOptions& options)
    {
        if (options.danger)
            return StopAction(options);
        MainActionResult result;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 end(at.x + options.width, at.y + height::Main);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        result.mainCentre = ImVec2((at.x + end.x) / 2, (at.y + end.y) / 2);
        result.gearCentre = ImVec2(end.x + space::S + height::MainGear / 2, result.mainCentre.y);
        ImGui::BeginDisabled(!options.enabled);
        if (Hit("##main", ImVec2(options.width, height::Main)))
            result.hit = MainActionHit::Main;
        ImGui::EndDisabled();
        State state = ItemState(!options.enabled);
        if (options.takeFocus && options.enabled)
        {
            FocusLastItem(options.showFocus);
            state.focused = options.showFocus;
        }
        const bool busy = options.progress >= 0 || options.indeterminate;
        // In the shown game's colours (its logo's, StyleOf) when it has them: so that each game's page has its own
        // action.
        const bool styled = options.style != nullptr && !busy;
        const unsigned fill = busy                             ? color::OverArt
                              : state.pressed                  ? color::AccentFillPressed
                              : state.hovered || state.focused ? color::AccentFillHover
                                                               : color::AccentFill;
        unsigned ink = busy ? color::Ink : color::OnAccent;
        if (styled)
        {
            const LauncherTheme::GameStyle& style = *options.style;
            const unsigned top = Mix(style.top, style.bottom, 0.2f), bottom = Mix(style.top, style.bottom, 0.58f);
            Look look;
            look.enabled = options.enabled;
            look.hovered = options.enabled && (state.hovered || state.focused);
            look.held = options.enabled && state.pressed;
            GradientRect(draw, at, end, Col(Shade(top, look), options.enabled ? 1 : DisabledAlpha),
                Col(Shade(bottom, look), options.enabled ? 1 : DisabledAlpha), radius::Card,
                ImDrawFlags_RoundCornersAll);
            draw->AddLine(ImVec2(at.x + radius::Card, at.y + 0.5f), ImVec2(end.x - radius::Card, at.y + 0.5f),
                Col(0xFFFFFF, options.enabled ? 0.27f : 0.1f));
            ink = InkOn(Mix(top, bottom, 0.5f), style.outline);
        }
        else
            draw->AddRectFilled(at, end, Col(fill, options.enabled || busy ? 1 : DisabledAlpha), radius::Card);
        std::string label = options.label;
        if (options.progress >= 0 && !options.indeterminate)
            label += " " + FormatPercent(options.progress);
        const float lead = options.icon == Icon::None ? 0 : icon::Normal + space::S;
        ImGui::PushFont(Ctx().fonts.title,
            FitSize(Ctx().fonts.title, type::Title, label.c_str(), options.width - 2 * space::M - lead));
        const float total = ImGui::CalcTextSize(label.c_str()).x + lead;
        if (lead > 0)
            DrawIcon(draw, options.icon,
                ImVec2(result.mainCentre.x - total / 2 + icon::Normal / 2, result.mainCentre.y), icon::Normal,
                Col(ink));
        DrawCapsLabel(
            draw, ImVec2(at.x + lead, at.y), end, label.c_str(), Col(ink, options.enabled || busy ? 1 : DisabledAlpha));
        ImGui::PopFont();
        if (options.progress >= 0 || options.indeterminate)
            DrawProgress(draw, at, end, radius::Card, 0, Ctx().accent, options.progress, options.indeterminate);
        FocusRingIf(state, radius::Card);

        const ImVec2 gearAt(end.x + space::S, at.y), gearEnd(gearAt.x + height::MainGear, end.y);
        ImGui::SetCursorScreenPos(gearAt);
        if (Hit("##gamesettings", ImVec2(height::MainGear, height::Main)))
            result.hit = MainActionHit::Settings;
        State gear = ItemState();
        if (options.takeFocus && !options.enabled)
        {
            FocusLastItem(options.showFocus);
            gear.focused = options.showFocus;
        }
        draw->AddRectFilled(gearAt, gearEnd,
            Col(gear.pressed   ? color::OverArtPressed
                : gear.hovered ? color::OverArtHover
                               : color::OverArt),
            radius::Card);
        draw->AddRect(gearAt, gearEnd, Col(color::OverArtEdge), radius::Card, 0, stroke::Hairline);
        DrawIcon(draw, Icon::Gear, result.gearCentre, icon::Large, Col(color::Ink));
        FocusRingIf(gear, radius::Card);
        if (options.gearTooltip != nullptr && WantsTooltip(gear))
            Bubble(options.gearTooltip, gearAt, gearEnd);
        return result;
    }
}
