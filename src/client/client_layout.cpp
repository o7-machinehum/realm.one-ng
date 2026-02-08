#include "client_layout.h"

#include <algorithm>
#include <cmath>

namespace client {

PlayLayout computePlayLayout(const std::optional<Room>& current_room,
                             int side_w,
                             int bottom_h,
                             float max_width_ratio,
                             float max_height_ratio,
                             float fallback_scale) {
    PlayLayout out{};

    const int left_region_w = std::max(1, GetScreenWidth() - side_w);
    const int left_region_h = std::max(1, GetScreenHeight() - bottom_h);
    const int max_play_w = std::max(1, static_cast<int>(left_region_w * max_width_ratio));
    const int max_play_h = std::max(1, static_cast<int>(left_region_h * max_height_ratio));

    float map_px_w = 0.0f;
    float map_px_h = 0.0f;
    float map_aspect = static_cast<float>(max_play_w) / static_cast<float>(max_play_h);
    if (current_room) {
        map_px_w = static_cast<float>(current_room->map_width() * current_room->tile_width());
        map_px_h = static_cast<float>(current_room->map_height() * current_room->tile_height());
        if (map_px_w > 0.0f && map_px_h > 0.0f) {
            map_aspect = map_px_w / map_px_h;
        }
    }

    int play_w = max_play_w;
    int play_h = std::max(1, static_cast<int>(std::lround(play_w / map_aspect)));
    if (play_h > max_play_h) {
        play_h = max_play_h;
        play_w = std::max(1, static_cast<int>(std::lround(play_h * map_aspect)));
    }

    const int play_x = std::max(0, (left_region_w - play_w) / 2);
    const int play_y = std::max(0, (left_region_h - play_h) / 2);
    out.play_rect = Rectangle{
        static_cast<float>(play_x),
        static_cast<float>(play_y),
        static_cast<float>(play_w),
        static_cast<float>(play_h)
    };

    out.render_scale = fallback_scale;
    out.map_draw_w = static_cast<float>(play_w);
    out.map_draw_h = static_cast<float>(play_h);

    float map_off_x = 0.0f;
    float map_off_y = 0.0f;
    if (current_room && map_px_w > 0.0f && map_px_h > 0.0f) {
        const float fill_width_scale = static_cast<float>(play_w) / map_px_w;
        const float fill_height_scale = static_cast<float>(play_h) / map_px_h;
        const float fit_scale = std::min(fill_width_scale, fill_height_scale);
        out.render_scale = std::max(0.5f, fit_scale);
        out.map_draw_w = map_px_w * out.render_scale;
        out.map_draw_h = map_px_h * out.render_scale;
        map_off_x = std::max(0.0f, (static_cast<float>(play_w) - out.map_draw_w) * 0.5f);
        map_off_y = std::max(0.0f, (static_cast<float>(play_h) - out.map_draw_h) * 0.5f);
    }

    out.map_origin_x = static_cast<float>(play_x) + map_off_x;
    out.map_origin_y = static_cast<float>(play_y) + map_off_y;
    return out;
}

void snapDockedWindows(const std::vector<UiWindow*>& windows,
                       float right_x,
                       float top_margin,
                       float gap_y) {
    std::vector<UiWindow*> sorted = windows;
    std::sort(sorted.begin(), sorted.end(), [](const UiWindow* a, const UiWindow* b) {
        return a->rect.y < b->rect.y;
    });

    float y = top_margin;
    for (UiWindow* w : sorted) {
        w->rect.x = right_x;
        w->rect.y = y;
        y += windowFrameHeight(*w) + gap_y;
    }
}

} // namespace client
