#include "raylib.h"
#include "room.h"
#include "room_render.h"
#include "net_client.h"
#include "msg.h"

#include <deque>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    InitWindow(960, 640, "The Island");
    SetTargetFPS(60);

    Mailbox mailbox;
    RoomRenderer rr;
    NetClient nc(mailbox, "127.0.0.1", 7000);
    nc.start();

    std::optional <Room> currentRoom;
    std::deque<std::string> logs;
    std::string input;
    constexpr int panelHeight = 140;

    auto pushLog = [&logs](std::string line) {
        logs.push_back(std::move(line));
        while (logs.size() > 6) logs.pop_front();
    };

    pushLog("Type /login <user> <pass> to authenticate.");

    while (!WindowShouldClose()) {
        while (int key = GetCharPressed()) {
            if (key >= 32 && key <= 126 && input.size() < 120) {
                input.push_back(static_cast<char>(key));
            }
        }

        if (IsKeyPressed(KEY_BACKSPACE) && !input.empty()) {
            input.pop_back();
        }

        if (IsKeyPressed(KEY_ENTER)) {
            if (!input.empty()) {
                if (input.rfind("/login ", 0) == 0) {
                    std::istringstream iss(input);
                    std::string cmd, user, pass;
                    iss >> cmd >> user >> pass;
                    if (!user.empty() && !pass.empty()) {
                        mailbox.push(MsgType::Login, LoginMsg{user, pass});
                        pushLog("Sent login for user: " + user);
                    } else {
                        pushLog("Usage: /login <user> <pass>");
                    }
                } else if (input == "/help") {
                    pushLog("Available commands: /login <user> <pass>");
                } else {
                    pushLog("Unknown command. Try /help.");
                }
            }
            input.clear();
        }

        if(auto login = mailbox.pop<LoginResultMsg>(MsgType::LoginResult)) {
            pushLog(login->ok
                ? ("Login success. User: " + login->user + " room: " + login->room)
                : ("Login failed: " + login->message));
        }

        BeginDrawing();
        ClearBackground(BLACK);

        // If we recieve a new room
        // If we recieve a new room
        if(auto room = mailbox.pop<Room>(MsgType::Room)) {
            std::cout << "got a room in main!" << std::endl;
            currentRoom = std::move(*room);
            rr.load(*currentRoom);
        }

        if (currentRoom) {
            rr.draw(*currentRoom, 2.0f);
        }

        DrawRectangle(0, GetScreenHeight() - panelHeight, GetScreenWidth(), panelHeight, Fade(BLACK, 0.9f));
        DrawRectangleLines(0, GetScreenHeight() - panelHeight, GetScreenWidth(), panelHeight, GRAY);

        int y = GetScreenHeight() - panelHeight + 10;
        for (const auto& line : logs) {
            DrawText(line.c_str(), 10, y, 18, RAYWHITE);
            y += 20;
        }

        std::string prompt = "> " + input;
        DrawText(prompt.c_str(), 10, GetScreenHeight() - 28, 20, YELLOW);

        EndDrawing();
    }

    nc.stop();
    CloseWindow();
    return 0;
}
