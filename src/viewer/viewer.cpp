#include "raylib.h"

#include "cube_renderer.h"
#include "voxel_world.h"

#include <cstdio>
#include <string>

namespace {
constexpr int kInitialWindowW = 1280;
constexpr int kInitialWindowH = 800;
constexpr float kPanSpeedTilesPerSec = 8.0f;
}

int main(int argc, char** argv) {
    std::string world_path = "data/world.dat";
    if (argc >= 2) world_path = argv[1];

    voxel::World world;
    std::string err;
    if (!voxel::load(world, world_path, &err)) {
        std::fprintf(stderr, "FATAL: failed to load %s: %s\n", world_path.c_str(), err.c_str());
        std::fprintf(stderr, "Run worldgen first, e.g.: ./build/worldgen --out data/world.dat\n");
        return 1;
    }

    InitWindow(kInitialWindowW, kInitialWindowH, "Cube Viewer");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    cubes::TextureCache textures;
    cubes::StepParams steps = cubes::kDefaultSteps;
    cubes::RenderParams render = cubes::kDefaultRender;
    cubes::Camera cam;
    cam.center_x = world.header.size_x * 0.5f;
    cam.center_y = world.header.size_y * 0.5f;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        float vx = 0, vy = 0;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    vy -= 1;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  vy += 1;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  vx -= 1;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) vx += 1;
        cam.center_x += vx * kPanSpeedTilesPerSec * dt;
        cam.center_y += vy * kPanSpeedTilesPerSec * dt;

        BeginDrawing();
        ClearBackground(Color{20, 24, 32, 255});
        cubes::drawWorld(world, cam, textures, steps, render);

        DrawText(TextFormat("world: %ux%ux%u   cam: (%.1f, %.1f)   WASD to pan",
                            world.header.size_x, world.header.size_y, world.header.size_z,
                            cam.center_x, cam.center_y),
                 8, 8, 16, RAYWHITE);
        EndDrawing();
    }

    textures.unloadAll();
    CloseWindow();
    return 0;
}
