#include "raylib.h"
#include "room.h"
#include "room_render.h"

#include <string>

int main()
{
    InitWindow(960, 640, "test_room: 500_500/rooms/0.tmx");
    SetTargetFPS(60);

    Room room;
    if (!room.loadFromFile("data/worlds/500_500/rooms/0.tmx")) return 1;

    RoomRenderer rr;
    if (!rr.load(room)) return 1;

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(DARKGRAY);
        rr.draw(room, 2.0f);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
