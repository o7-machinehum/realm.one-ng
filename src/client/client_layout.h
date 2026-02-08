#pragma once

#include "raylib.h"
#include "room.h"
#include "client_windows.h"

#include <optional>
#include <vector>

namespace client {

struct PlayLayout {
    Rectangle play_rect{};
    float render_scale = 1.0f;
    float map_draw_w = 0.0f;
    float map_draw_h = 0.0f;
    float map_origin_x = 0.0f;
    float map_origin_y = 0.0f;
};

PlayLayout computePlayLayout(const std::optional<Room>& current_room,
                             int side_w,
                             int bottom_h,
                             float max_width_ratio,
                             float max_height_ratio,
                             float fallback_scale);

void snapDockedWindows(const std::vector<UiWindow*>& windows,
                       float right_x,
                       float top_margin,
                       float gap_y);

} // namespace client
