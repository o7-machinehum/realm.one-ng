#include "raylib.h"

#include "cube_renderer.h"
#include "voxel_world.h"

#include <cstdio>
#include <string>

namespace {

constexpr int   kInitialWindowW       = 1280;
constexpr int   kInitialWindowH       = 800;
constexpr float kPanSpeedTilesPerSec  = 8.0f;
constexpr Color kBackgroundColor      = {20, 24, 32, 255};

void panCameraFromInput(cubes::Camera& cam, float dt) {
    float vx = 0.0f, vy = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    vy -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  vy += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  vx -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) vx += 1.0f;
    cam.center_x += vx * kPanSpeedTilesPerSec * dt;
    cam.center_y += vy * kPanSpeedTilesPerSec * dt;
}

void drawHud(const voxel::World& world, const cubes::Camera& cam) {
    DrawText(TextFormat("world: %ux%ux%u   cam: (%.1f, %.1f)   WASD to pan",
                        world.header.size_x, world.header.size_y, world.header.size_z,
                        cam.center_x, cam.center_y),
             8, 8, 16, RAYWHITE);
}

bool loadWorldOrReport(voxel::World& world, const std::string& path) {
    std::string err;
    if (voxel::load(world, path, &err)) return true;
    std::fprintf(stderr, "FATAL: failed to load %s: %s\n", path.c_str(), err.c_str());
    std::fprintf(stderr, "Run worldgen first, e.g.: ./build/worldgen --out data/world.dat\n");
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const std::string world_path = (argc >= 2) ? argv[1] : "data/world.dat";

    voxel::World world;
    if (!loadWorldOrReport(world, world_path)) return 1;

    InitWindow(kInitialWindowW, kInitialWindowH, "Cube Viewer");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    cubes::TextureCache textures;
    cubes::StepParams steps = cubes::kDefaultSteps;
    cubes::RenderParams render = cubes::kDefaultRender;
    cubes::Camera cam{
        world.header.size_x * 0.5f,
        world.header.size_y * 0.5f,
    };

    while (!WindowShouldClose()) {
        panCameraFromInput(cam, GetFrameTime());

        const cubes::Projection proj{steps, render, cam, GetScreenWidth(), GetScreenHeight()};

        BeginDrawing();
        ClearBackground(kBackgroundColor);
        cubes::drawWorld(world, proj, textures);
        drawHud(world, cam);
        EndDrawing();
    }

    textures.unloadAll();
    CloseWindow();
    return 0;
}
