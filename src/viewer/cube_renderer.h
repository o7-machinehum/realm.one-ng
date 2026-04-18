#pragma once

#include "raylib.h"
#include "voxel_world.h"

#include <string>
#include <unordered_map>

namespace cubes {

// Per-axis screen-space step in *unscaled* pixels for a unit move along
// world x, world y, and world z. Tweak these to dial in cube overlap.
//
// Defaults are sized for the 17x17 isometric cube tile in
// game/assets/cubes/tile-1.png. They assume:
//   - +world_x  : right and slightly down
//   - +world_y  : left and slightly down
//   - +world_z  : straight up
struct StepParams {
    float x_dx =  8.0f;
    float x_dy =  4.0f;
    float y_dx = -8.0f;
    float y_dy =  4.0f;
    float z_dx =  0.0f;
    float z_dy = -8.0f;
};

// Camera/render config.
struct RenderParams {
    float scale          = 4.0f;  // raylib draw scale
    int   view_tiles_x   = 30;    // approximate visible width  (in cubes)
    int   view_tiles_y   = 15;    // approximate visible height (in cubes)
    int   tile_pixels    = 17;    // raw tile image size
};

constexpr StepParams   kDefaultSteps{};
constexpr RenderParams kDefaultRender{};

// Texture cache keyed by the CubeDef.texture string.
struct TextureCache {
    std::unordered_map<std::string, Texture2D> by_path;
    Texture2D get(const std::string& asset_relative_path); // load on first use
    void unloadAll();
};

// Camera centered on a world tile (cx, cy) at floor (z=0).
struct Camera {
    float center_x = 0.0f;
    float center_y = 0.0f;
};

// Convert a world (x,y,z) to screen pixels given camera and step/render params.
Vector2 worldToScreen(float wx, float wy, float wz,
                      const Camera& cam,
                      const StepParams& steps,
                      const RenderParams& render,
                      int screen_w, int screen_h);

// Draw the entire visible portion of `world` to the current raylib frame.
void drawWorld(const voxel::World& world,
               const Camera& cam,
               TextureCache& cache,
               const StepParams& steps,
               const RenderParams& render);

} // namespace cubes
