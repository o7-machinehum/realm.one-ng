#pragma once

#include "raylib.h"
#include "room.h"

#include <optional>

namespace client {

struct PlayLayout {
    Rectangle play_rect{};
    float render_scale = 1.0f;
    float map_draw_w = 0.0f;
    float map_draw_h = 0.0f;
    float map_origin_x = 0.0f;
    float map_origin_y = 0.0f;
};

// Computes play viewport filling the entire screen, fitting the room map.
// No side panels or bottom margins — the map is scaled to fill the screen.
PlayLayout computeFullscreenLayout(const std::optional<Room>& current_room,
                                   float fallback_scale);

} // namespace client
