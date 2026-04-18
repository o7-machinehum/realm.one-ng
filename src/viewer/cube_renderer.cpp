#include "cube_renderer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace cubes {

Texture2D TextureCache::get(const std::string& asset_relative_path) {
    auto it = by_path.find(asset_relative_path);
    if (it != by_path.end()) return it->second;
    const std::string full = "game/assets/" + asset_relative_path;
    Texture2D tex = LoadTexture(full.c_str());
    if (tex.id != 0) SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    by_path.emplace(asset_relative_path, tex);
    return tex;
}

void TextureCache::unloadAll() {
    for (auto& kv : by_path) {
        if (kv.second.id != 0) UnloadTexture(kv.second);
    }
    by_path.clear();
}

Vector2 worldToScreen(float wx, float wy, float wz,
                      const Camera& cam,
                      const StepParams& s,
                      const RenderParams& r,
                      int screen_w, int screen_h) {
    const float dx = wx - cam.center_x;
    const float dy = wy - cam.center_y;
    const float raw_x = dx * s.x_dx + dy * s.y_dx + wz * s.z_dx;
    const float raw_y = dx * s.x_dy + dy * s.y_dy + wz * s.z_dy;
    return Vector2{
        screen_w * 0.5f + raw_x * r.scale,
        screen_h * 0.5f + raw_y * r.scale,
    };
}

void drawWorld(const voxel::World& world,
               const Camera& cam,
               TextureCache& cache,
               const StepParams& s,
               const RenderParams& r) {
    const int screen_w = GetScreenWidth();
    const int screen_h = GetScreenHeight();

    // Painter's algorithm: smaller (x+y) is farther from camera, draw first.
    // For same diagonal, draw lower z first so higher z occludes correctly.
    const uint32_t sx = world.header.size_x;
    const uint32_t sy = world.header.size_y;
    const uint32_t sz = world.header.size_z;

    const float draw_w = static_cast<float>(r.tile_pixels) * r.scale;
    const float draw_h = static_cast<float>(r.tile_pixels) * r.scale;
    // Approximate cube screen footprint for off-screen culling. Be generous.
    const float cull_pad = std::max(draw_w, draw_h);

    for (uint32_t diag = 0; diag < sx + sy - 1; ++diag) {
        const uint32_t x_lo = (diag >= sy - 1) ? diag - (sy - 1) : 0;
        const uint32_t x_hi = std::min(diag, sx - 1);
        for (uint32_t z = 0; z < sz; ++z) {
            for (uint32_t x = x_lo; x <= x_hi; ++x) {
                const uint32_t y = diag - x;
                const uint16_t id = world.at(x, y, z);
                if (id == 0) continue;
                if (id - 1 >= world.defs.size()) continue;
                const auto& def = world.defs[id - 1];

                Texture2D tex = cache.get(def.texture);
                if (tex.id == 0) continue;

                const Vector2 origin = worldToScreen(
                    static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                    cam, s, r, screen_w, screen_h);
                // Center the cube image on the (x,y,z) screen point.
                const float draw_x = origin.x - draw_w * 0.5f;
                const float draw_y = origin.y - draw_h * 0.5f;
                if (draw_x + draw_w < -cull_pad || draw_x > screen_w + cull_pad) continue;
                if (draw_y + draw_h < -cull_pad || draw_y > screen_h + cull_pad) continue;

                Rectangle src;
                if (def.src_w > 0 && def.src_h > 0) {
                    src = Rectangle{
                        static_cast<float>(def.src_x), static_cast<float>(def.src_y),
                        static_cast<float>(def.src_w), static_cast<float>(def.src_h)};
                } else {
                    src = Rectangle{0, 0,
                        static_cast<float>(tex.width), static_cast<float>(tex.height)};
                }
                const Rectangle dst{draw_x, draw_y, draw_w, draw_h};
                DrawTexturePro(tex, src, dst, Vector2{0, 0}, 0.0f, WHITE);
            }
        }
    }
}

} // namespace cubes
