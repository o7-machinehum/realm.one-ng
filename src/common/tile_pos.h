// Tile-space position used by server runtime structs and spatial queries.
#pragma once

#include <cstdlib>

struct TilePos {
    int x = 0;
    int y = 0;

    bool operator==(const TilePos& o) const { return x == o.x && y == o.y; }
    bool operator!=(const TilePos& o) const { return !(*this == o); }

    // Manhattan distance to another position.
    int distanceTo(const TilePos& o) const {
        return std::abs(x - o.x) + std::abs(y - o.y);
    }

    // Chebyshev (chess-king) distance to another position.
    int chebyshevTo(const TilePos& o) const {
        const int dx = std::abs(x - o.x);
        const int dy = std::abs(y - o.y);
        return (dx > dy) ? dx : dy;
    }

    // Return position offset by (dx, dy).
    TilePos offset(int dx, int dy) const { return {x + dx, y + dy}; }
};
