#pragma once

// The pieces a figure's card and a portal seat share: the frame, the picture's placeholder and a seat's three lines.
#include "launcher/ui/components/core.h"
#include <cmath>

namespace ui
{
    inline ImU32 FigureCardFill(bool raised)
    {
        return Col(raised ? color::Raised : color::Surface, float(elevation::Card.fillAlpha) / 255.0f);
    }

    struct FigureFrameOptions
    {
        bool raised = false; // pointed at or focused: the raised fill
        bool accent = false; // on a seat, being chosen for, a drop's target: the accent edge
        bool edged = false;  // pointed at or selected: the stronger edge colour
        bool thick = false;
    };
    inline void DrawFigureFrame(ImDrawList* draw, ImVec2 min, ImVec2 max, const FigureFrameOptions& options)
    {
        draw->AddRectFilled(min, max, FigureCardFill(options.raised), radius::Card);
        draw->AddRect(min, max,
            Col(options.accent  ? color::AccentFill
                : options.edged ? color::Edge
                                : color::Hairline),
            radius::Card, 0, options.thick ? stroke::Edge : stroke::Hairline);
    }

    // In the picture's place when there is none: an icon on the inset, outlined when pressing it fills the seat.
    inline void DrawFigurePlaceholder(ImDrawList* draw, ImVec2 at, float size, Icon icon, bool outlined, bool lit)
    {
        const ImVec2 max(at.x + size, at.y + size);
        draw->AddRectFilled(at, max, Col(color::Inset), radius::Control);
        if (outlined)
            draw->AddRect(at, max, Col(lit ? color::AccentFill : color::Hairline), radius::Control, 0, stroke::Edge);
        DrawIcon(draw, icon, ImVec2(at.x + size / 2, at.y + size / 2), icon::Normal,
            Col(lit ? color::AccentFill : color::Dim));
    }

    // Where a seat's picture and its three lines (the seat, the figure, its detail) go.
    struct SeatLayout
    {
        ImVec2 picture;
        float textX, room;
        float first, second, third;

        SeatLayout(ImVec2 min, ImVec2 max, float padding, float pictureSize, float height)
            : picture(min.x + padding, min.y + padding),
              textX(picture.x + pictureSize + space::S),
              room(max.x - textX - padding),
              first(min.y + std::round((height - (2 * type::Caption + type::Body + 2 * space::XXS)) / 2)),
              second(first + type::Caption + space::XXS),
              third(second + type::Body + space::XXS)
        {}
    };
}
