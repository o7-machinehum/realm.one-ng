#include "cube_renderer.h"

#include <algorithm>
#include <cstdlib>

namespace cubes {

namespace {

const char* const kAssetRootDir = "game/assets/";

float cubeDrawSize(const RenderParams& r) {
    return static_cast<float>(r.tile_pixels) * r.scale;
}

bool isOnScreen(float draw_x, float draw_y, float draw_w, float draw_h,
                int screen_w, int screen_h) {
    const float pad = std::max(draw_w, draw_h);
    if (draw_x + draw_w < -pad || draw_x > screen_w + pad) return false;
    if (draw_y + draw_h < -pad || draw_y > screen_h + pad) return false;
    return true;
}

bool isOccludingPlayer(int x, int y, int z, const FadeRule& fade) {
    return fade.enabled
        && z > fade.center_z
        && std::abs(x - fade.center_x) <= fade.radius_xy
        && std::abs(y - fade.center_y) <= fade.radius_xy;
}

Rectangle sourceRect(const voxel::CubeDef& def, const Texture2D& tex) {
    if (def.src_w > 0 && def.src_h > 0) {
        return Rectangle{
            static_cast<float>(def.src_x), static_cast<float>(def.src_y),
            static_cast<float>(def.src_w), static_cast<float>(def.src_h)};
    }
    return Rectangle{0, 0,
        static_cast<float>(tex.width), static_cast<float>(tex.height)};
}

// Returns false if cube should be skipped (out of frame, missing texture, etc.).
bool drawCube(int x, int y, int z, uint16_t cube_id,
              const voxel::World& world, TextureCache& cache,
              const Projection& proj, const FadeRule& fade) {
    if (cube_id == voxel::kAirCubeId) return false;
    const size_t def_index = static_cast<size_t>(cube_id) - 1;
    if (def_index >= world.defs.size()) return false;
    const auto& def = world.defs[def_index];

    Texture2D tex = cache.get(def.texture);
    if (tex.id == 0) return false;

    const float draw_w = cubeDrawSize(proj.render);
    const float draw_h = cubeDrawSize(proj.render);
    const Vector2 origin = worldToScreen(static_cast<float>(x),
                                         static_cast<float>(y),
                                         static_cast<float>(z),
                                         proj);
    const float draw_x = origin.x - draw_w * 0.5f;
    const float draw_y = origin.y - draw_h * 0.5f;
    if (!isOnScreen(draw_x, draw_y, draw_w, draw_h, proj.screen_w, proj.screen_h)) return false;

    Color tint = WHITE;
    if (isOccludingPlayer(x, y, z, fade)) tint.a = fade.alpha;

    DrawTexturePro(tex, sourceRect(def, tex),
                   Rectangle{draw_x, draw_y, draw_w, draw_h},
                   Vector2{0, 0}, 0.0f, tint);
    return true;
}

void drawDiagonalSlice(uint32_t diag, uint32_t z,
                       const voxel::World& world, TextureCache& cache,
                       const Projection& proj, const FadeRule& fade) {
    const uint32_t sx = world.header.size_x;
    const uint32_t sy = world.header.size_y;
    const uint32_t x_lo = (diag >= sy - 1) ? diag - (sy - 1) : 0;
    const uint32_t x_hi = std::min(diag, sx - 1);

    for (uint32_t x = x_lo; x <= x_hi; ++x) {
        const uint32_t y = diag - x;
        drawCube(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                 world.at(x, y, z), world, cache, proj, fade);
    }
}

} // namespace

Texture2D TextureCache::get(const std::string& asset_relative_path) {
    auto it = by_path.find(asset_relative_path);
    if (it != by_path.end()) return it->second;

    const std::string full = std::string(kAssetRootDir) + asset_relative_path;
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

Vector2 worldToScreen(float wx, float wy, float wz, const Projection& p) {
    const float dx = wx - p.camera.center_x;
    const float dy = wy - p.camera.center_y;
    const float raw_x = dx * p.steps.x_dx + dy * p.steps.y_dx + wz * p.steps.z_dx;
    const float raw_y = dx * p.steps.x_dy + dy * p.steps.y_dy + wz * p.steps.z_dy;
    return Vector2{
        p.screen_w * 0.5f + raw_x * p.render.scale,
        p.screen_h * 0.5f + raw_y * p.render.scale,
    };
}

void drawWorld(const voxel::World& world,
               const Projection& proj,
               TextureCache& cache,
               const FadeRule& fade,
               const EntityDrawFn& entities) {
    const uint32_t sx = world.header.size_x;
    const uint32_t sy = world.header.size_y;
    const uint32_t sz = world.header.size_z;

    // Painter's algorithm: smaller (x+y) is farther from camera; same diagonal,
    // lower z first so higher z occludes correctly.
    for (uint32_t diag = 0; diag < sx + sy - 1; ++diag) {
        for (uint32_t z = 0; z < sz; ++z) {
            drawDiagonalSlice(diag, z, world, cache, proj, fade);
            if (entities) entities(static_cast<int>(diag), static_cast<int>(z));
        }
    }
}

} // namespace cubes
