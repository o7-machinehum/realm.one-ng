#pragma once

#include "raylib.h"
#include "voxel_world.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace cubes {

// Per-axis screen-space step in unscaled pixels for a unit move along
// world x, y, z. Tunable so cube overlap can be dialed in.
struct StepParams {
    float x_dx =  8.0f;
    float x_dy =  4.0f;
    float y_dx = -8.0f;
    float y_dy =  4.0f;
    float z_dx =  0.0f;
    float z_dy = -8.0f;
};

struct RenderParams {
    float scale        = 4.0f;
    int   view_tiles_x = 30;
    int   view_tiles_y = 15;
    int   tile_pixels  = 17;
};

constexpr StepParams   kDefaultSteps{};
constexpr RenderParams kDefaultRender{};

struct Camera {
    float center_x = 0.0f;
    float center_y = 0.0f;
};

// All inputs the projection needs in one bag.
struct Projection {
    StepParams   steps;
    RenderParams render;
    Camera       camera;
    int          screen_w;
    int          screen_h;
};

Vector2 worldToScreen(float wx, float wy, float wz, const Projection& proj);

// Cubes within radius_xy of (center_x, center_y) AND strictly above center_z
// are drawn at this alpha. Used so the local player isn't hidden by terrain.
struct FadeRule {
    bool    enabled    = false;
    int     center_x   = 0;
    int     center_y   = 0;
    int     center_z   = 0;
    int     radius_xy  = 1;
    uint8_t alpha      = 100;
};

struct TextureCache {
    std::unordered_map<std::string, Texture2D> by_path;
    Texture2D get(const std::string& asset_relative_path);
    void unloadAll();
};

// Invoked once per (diagonal=x+y, z) layer after the cubes at that layer are
// drawn. Caller draws any entities that should appear at that depth slice.
using EntityDrawFn = std::function<void(int diag, int z)>;

void drawWorld(const voxel::World& world,
               const Projection& proj,
               TextureCache& cache,
               const FadeRule& fade = {},
               const EntityDrawFn& entities = nullptr);

} // namespace cubes
