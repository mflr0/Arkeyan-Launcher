#pragma once

// The portal's seats on the Collection page: one row, or two when a seat's label would be cut on one. Two rows hold the
// players' seats (players and vehicles) first, then the shared ones (item, adventure, trap); each row shares the whole
// width. Only the drawing order changes: the seats keep their indices.
#include <algorithm>
#include <cmath>
#include <vector>

namespace launcher_ui
{
    // The width of each of `count` seats sharing `width`, `gap` between two.
    inline float SeatWidth(float width, float gap, size_t count)
    {
        return count == 0 ? 0 : std::floor((width - gap * float(count - 1)) / float(count));
    }

    // The seats as drawn, row by row (seat indices). `textRoom(seatWidth)`: the room a seat that wide leaves its label;
    // `widest`: the widest label.
    template <typename TextRoom>
    std::vector<std::vector<size_t>> SeatRows(
        const std::vector<bool>& playerSeat, float width, float gap, float widest, TextRoom&& textRoom)
    {
        std::vector<std::vector<size_t>> rows(2);
        for (size_t i = 0; i < playerSeat.size(); i++)
            rows[playerSeat[i] ? 0 : 1].push_back(i);
        const bool fits = textRoom(SeatWidth(width, gap, playerSeat.size())) >= widest;
        if (fits || rows[0].empty() || rows[1].empty())
        {
            rows.assign(1, {});
            for (size_t i = 0; i < playerSeat.size(); i++)
                rows[0].push_back(i);
        }
        return rows;
    }
}
