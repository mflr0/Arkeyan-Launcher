// Home's big pieces (cards.h).
#include "launcher/ui/components/cards.h"
#include <algorithm>
#include <string>

namespace ui
{
    namespace
    {
        constexpr float LogoShare = 0.76f; // of the card's width
        constexpr float NameShare = 0.84f;
    }

    float BannerHeadingHeight() { return type::Display + space::S; }
    float BannerBlockHeight() { return BannerHeadingHeight() + height::Large + space::L; }

    bool Banner(ImVec2 at, float width, const BannerOptions& options)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        PushText(TextStyle::Display);
        DrawText(draw, at, Col(color::Ink), options.heading);
        PopText();
        const float top = at.y + BannerHeadingHeight();
        const float buttonW = ButtonWidth(options.action, options.button);
        const float textW = std::max(1.0f, width - buttonW - space::L);
        auto fitted = [&](ImFont* font, float size, float y, const char* text, unsigned ink) {
            ImGui::PushFont(font, FitSize(font, size, text, textW));
            DrawText(draw, ImVec2(at.x, y), Col(ink), text);
            ImGui::PopFont();
        };
        fitted(Ctx().fonts.bold, options.bigTitle ? type::Title : type::Body, top, options.title, color::Ink);
        fitted(Ctx().fonts.text, type::Body, top + height::Large - type::Body - space::XXS, options.detail,
            color::InkSoft);
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - buttonW, top));
        return Button((std::string(options.action) + options.actionId).c_str(), options.button);
    }

    float GameCardNameSize(const char* name, float cardWidth)
    {
        return FitSize(Ctx().fonts.title, type::Display, name, cardWidth * NameShare);
    }

    GameCardState GameCardButton(const char* id, ImVec2 at, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(at);
        GameCardState state;
        state.pressed = Hit(id, size);
        state.hovered = ImGui::IsItemHovered();
        state.focused = ItemState().focused;
        if (state.hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return state;
    }

    void DrawGameCard(ImVec2 at, ImVec2 size, const GameCardState& state, const GameCardOptions& options)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(at.x + size.x, at.y + size.y);
        SoftShadow(draw, at, end, radius::Panel, space::XS, space::S, elevation::Raised.shadowAlpha);
        if (options.artSize.x > 0 && options.artSize.y > 0)
        {
            const float w = options.artSize.x, h = options.artSize.y;
            const float scale = std::max(size.x / w, size.y / h), u = size.x / (w * scale), v = size.y / (h * scale);
            draw->AddImageRounded(options.art, at, end, ImVec2((1 - u) / 2, (1 - v) / 2),
                ImVec2((1 + u) / 2, (1 + v) / 2), Col(0xFFFFFF, state.hovered ? 1.0f : 0.88f), radius::Panel);
            draw->AddRectFilled(at, end, Col(color::Scrim, options.artDim), radius::Panel);
        }
        else
            GradientRect(draw, at, end, options.fillTop, options.fillBottom, radius::Panel);
        // The bottom fades to dark for the state; the logo sits in the upper part.
        GradientRect(draw, ImVec2(at.x, at.y + size.y * 0.45f), end, Col(color::Scrim, 0), Col(color::Scrim, 0.92f),
            radius::Panel, ImDrawFlags_RoundCornersBottom);
        // What the state takes at the foot is left out of the logo's box.
        const float footH = space::S + PillHeight + space::XS;
        const float logoTop = at.y + space::S, logoH = std::max(0.0f, size.y - footH - space::S);
        if (options.logo)
            options.logo(ImVec2(at.x + size.x * 0.12f, logoTop), ImVec2(size.x * LogoShare, logoH));
        else
        {
            ImGui::PushFont(Ctx().fonts.title, options.nameSize);
            const ImVec2 text = ImGui::CalcTextSize(options.name);
            draw->AddText(
                ImVec2(at.x + (size.x - text.x) / 2, logoTop + (logoH - text.y) / 2), Col(color::Ink), options.name);
            ImGui::PopFont();
        }
        ImGui::SetCursorScreenPos(ImVec2(at.x + space::S, end.y - space::S - PillHeight));
        PushText(TextStyle::Caption);
        const std::string shown = Ellipsize(options.state, size.x - 2 * space::S - 2 * space::M - icon::Small);
        PopText();
        StatusPill(options.status, shown.c_str(), options.stateIcon);
        draw->AddRect(at, end, state.hovered ? options.hoverEdge : Col(color::Ink, 0.11f), radius::Panel, 0,
            state.hovered ? stroke::Selection : stroke::Hairline);
        if (state.focused) // around the card (its state pill is the last item drawn)
            FocusRing(at, end, radius::Panel);
    }
}
