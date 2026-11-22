#pragma once

// A player's Avatar and the Swatch that picks its colour.
#include "launcher/ui/components/core.h"
#include <algorithm>

namespace ui
{
    struct AvatarOptions
    {
        unsigned color = 0; // the player's: the ring, and the fill under the initials
        const char* initials = "";
        ImTextureRef picture; // in the circle in place of the initials
        bool hasPicture = false;
        ImVec2 uv0{ 0, 0 }, uv1{ 1, 1 };
    };
    inline void Avatar(ImDrawList* draw, ImVec2 centre, float radius, const AvatarOptions& options)
    {
        if (options.hasPicture)
        {
            draw->AddImageRounded(options.picture, ImVec2(centre.x - radius, centre.y - radius),
                ImVec2(centre.x + radius, centre.y + radius), options.uv0, options.uv1, IM_COL32_WHITE, radius);
            return;
        }
        draw->AddCircleFilled(centre, radius, Col(options.color));
        ImGui::PushFont(Ctx().fonts.title, radius * 0.8f);
        const ImVec2 extent = ImGui::CalcTextSize(options.initials);
        DrawText(draw, ImVec2(centre.x - extent.x / 2, centre.y - extent.y / 2), Col(InkOn(options.color, 0x14141A)),
            options.initials);
        ImGui::PopFont();
    }
    // The ring in the player's colour around an avatar.
    inline void AvatarRing(ImDrawList* draw, ImVec2 centre, float radius, unsigned rgb)
    {
        const bool large = radius > height::Large;
        draw->AddCircle(centre, radius + (large ? 4.0f : 2.5f), Col(rgb), 0, large ? 3.0f : 2.0f);
    }

    inline constexpr float SwatchSize = 24;
    inline float SwatchesWidth(int count) { return count * SwatchSize + std::max(0, count - 1) * space::XS; }
    // One colour to choose: a dot, ringed when chosen. True when pressed.
    inline bool Swatch(const char* id, unsigned rgb, bool selected)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool pressed = Hit(id, ImVec2(SwatchSize, SwatchSize));
        FocusRingIf(ItemState(), radius::Control);
        const ImVec2 centre(at.x + SwatchSize / 2, at.y + SwatchSize / 2);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(centre, SwatchSize / 2 - 2, Col(rgb));
        if (selected)
            draw->AddCircle(centre, SwatchSize / 2 + 0.5f, Col(0xFFFFFF), 0, 2.0f);
        return pressed;
    }
}
