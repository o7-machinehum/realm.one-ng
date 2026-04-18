#include "client_layout.h"

#include <algorithm>
#include <cmath>

namespace client {

PlayLayout computeFullscreenLayout(const std::optional<Room>& current_room,
                                   float fallback_scale) {
    PlayLayout out{};

    const int screen_w = GetScreenWidth();
    const int screen_h = GetScreenHeight();

    out.play_rect = Rectangle{0.0f, 0.0f,
                              static_cast<float>(screen_w),
                              static_cast<float>(screen_h)};
    out.render_scale = fallback_scale;
    out.map_draw_w = static_cast<float>(screen_w);
    out.map_draw_h = static_cast<float>(screen_h);
    out.map_origin_x = 0.0f;
    out.map_origin_y = 0.0f;

    if (!current_room) return out;

    const float map_px_w = static_cast<float>(current_room->map_width() * current_room->tile_width());
    const float map_px_h = static_cast<float>(current_room->map_height() * current_room->tile_height());
    if (map_px_w <= 0.0f || map_px_h <= 0.0f) return out;

    // Scale map to fill the entire screen (cover, not fit)
    const float scale_w = static_cast<float>(screen_w) / map_px_w;
    const float scale_h = static_cast<float>(screen_h) / map_px_h;
    out.render_scale = std::max(scale_w, scale_h);
    out.map_draw_w = map_px_w * out.render_scale;
    out.map_draw_h = map_px_h * out.render_scale;

    // Center the map (any overflow bleeds off-screen equally)
    out.map_origin_x = (static_cast<float>(screen_w) - out.map_draw_w) * 0.5f;
    out.map_origin_y = (static_cast<float>(screen_h) - out.map_draw_h) * 0.5f;

    return out;
}

} // namespace client
