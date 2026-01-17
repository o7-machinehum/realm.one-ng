#include <raylib.h>
#include "sprites.h"
#include "room.h"

int main() {
    InitWindow(960, 540, "quick_game");
    SetTargetFPS(60);

    SpriteAtlas atlas;
    atlas.loadDirectory("game/assets/art");

    Room room;
    room.loadFromFile("game/map/d1.tmx");

    Vector2 cam{0,0};
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (IsKeyDown(KEY_D)) cam.x += 300*dt;
        if (IsKeyDown(KEY_A)) cam.x -= 300*dt;
        if (IsKeyDown(KEY_S)) cam.y += 300*dt;
        if (IsKeyDown(KEY_W)) cam.y -= 300*dt;

        BeginDrawing();
        ClearBackground(RAYWHITE);

        room.draw(atlas, Vector2{-cam.x, -cam.y});

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

