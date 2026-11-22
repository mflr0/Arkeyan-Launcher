#pragma once

// The portal's seats on the Collection page: one row, or two when a seat's label would be cut on one. Two rows hold the
// players' seats (players and vehicles) first, then the shared ones (item, adventure, trap); each row shares the whole
// width. A narrow window splits a row again while its labels get less than the least room a label needs. Only the
// drawing order changes: the seats keep their indices.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace launcher_ui
{
    // The width of each of `count` seats sharing `width`, `gap` between two.
    inline float SeatWidth(float width, float gap, size_t count)
    {
        return count == 0 ? 0 : std::floor((width - gap * float(count - 1)) / float(count));
    }

    // The seats as drawn, row by row (seat indices). `textRoom(seatWidth)`: the room a seat that wide leaves its label;
    // `widest`: the widest label; `leastRoom`: the room under which a label is cut to nothing, so its row is split.
    template <typename TextRoom>
    std::vector<std::vector<size_t>> SeatRows(
        const std::vector<bool>& playerSeat, float width, float gap, float widest, float leastRoom, TextRoom&& textRoom)
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
        // Split each row into the fewest even rows that give a label `need` (one seat a row at worst), in order. The
        // need stops at `leastRoom`: a long label is ellipsized rather than stacking the seats in a wide window.
        const float need = std::min(widest, leastRoom);
        std::vector<std::vector<size_t>> split;
        for (const std::vector<size_t>& row : rows)
        {
            if (row.empty())
                continue;
            size_t perRow = row.size();
            while (perRow > 1 && textRoom(SeatWidth(width, gap, perRow)) < need)
                perRow--;
            const size_t parts = (row.size() + perRow - 1) / perRow;
            for (size_t p = 0, from = 0; p < parts; p++)
            {
                const size_t to =
                    from + (row.size() - from) / (parts - p) + ((row.size() - from) % (parts - p) ? 1 : 0);
                split.emplace_back(row.begin() + std::ptrdiff_t(from), row.begin() + std::ptrdiff_t(to));
                from = to;
            }
        }
        return split;
    }
}
