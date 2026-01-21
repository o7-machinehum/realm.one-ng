#include "raylib.h"
#include "room.h"
#include "room_render.h"
#include "net_client.h"
#include "msg.h"

#include <string>

int main()
{
    InitWindow(960, 640, "test_room: d1.tmx");
    SetTargetFPS(60);

    Mailbox mailbox;
    RoomRenderer rr;
    NetClient nc(mailbox, "127.0.0.1", 7000);
    nc.start();

    LoginMsg login = {
        .user = "Ryan",
        .pass = "Password",
    };

    mailbox.push(MsgType::Login, login);

    std::optional <Room> currentRoom;
    while (!WindowShouldClose()) {
        BeginDrawing();

        // If we recieve a new room
        if(auto room = mailbox.pop<Room>(MsgType::Room)) {
            std::cout << "got a room in main!" << std::endl;
            currentRoom = std::move(*room);
            rr.load(*currentRoom);
        }

        if (currentRoom) {
            rr.draw(*currentRoom, 2.0f);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
