// main.cpp
#include "raylib.h"
#include "sprites.h"

#include <iostream>
#include <string>

int main()
{
    InitWindow(800, 600, "player_1 animated");
    SetTargetFPS(60);

    Sprites sprites;
    if (!sprites.loadTSX("game/assets/art/character.tsx")) return 1;
    sprites.debug_dump();

    std::string texPath = "game/assets/art/" + sprites.image_source();
    Texture2D tex = LoadTexture(texPath.c_str());
    if (tex.id == 0) return 1;

    const int count = sprites.frame_count("player_1", Dir::S);
    if (count <= 0) return 1;

    // Feet position on a 16x16 grid
    float tile = 16.0f;
    float feetX = 8 * tile;
    float feetY = 8 * tile;

    // Animation
    int frameIndex = 0;
    float t = 0.0f;
    const float frameTime = 0.12f; // seconds per frame
    const float scale = 2.0f;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        // advance frame
        t += dt;
        while (t >= frameTime) {
            t -= frameTime;
            frameIndex = (frameIndex + 1) % count;
        }

        const Frame* fr = sprites.frame("player_1", Dir::S, frameIndex);

        Rectangle src = fr->rect();
        Rectangle dst = { feetX, feetY, src.width * scale, src.height * scale };
        Vector2 origin = { dst.width / 2.0f, dst.height };

        BeginDrawing();
        ClearBackground(DARKGRAY);

        // grid
        for (int y = 0; y < 600; y += 16) DrawLine(0, y, 800, y, Fade(WHITE, 0.2f));
        for (int x = 0; x < 800; x += 16) DrawLine(x, 0, x, 600, Fade(WHITE, 0.2f));

        DrawTexturePro(tex, src, dst, origin, 0.0f, WHITE);

        EndDrawing();
    }

    UnloadTexture(tex);
    CloseWindow();
    return 0;
}

