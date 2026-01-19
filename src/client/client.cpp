// src/main.cpp
#include <raylib.h>
#include <enet/enet.h>

#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "sprites.h"
#include "room.h"
#include "character.h"
#include "text_input.h"
#include "client_net.h"

static std::string trim(std::string s) {
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && is_space((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_space((unsigned char)s.back())) s.pop_back();
    return s;
}

int main() {
    // ---- ENet init ----
    if (enet_initialize() != 0) {
        std::cerr << "enet_initialize failed\n";
        return 1;
    }

    // ---- Window ----
    const int screenW = 960;
    const int screenH = 905;
    InitWindow(screenW, screenH, "quick_game_client");
    SetTargetFPS(60);

    // ---- Assets (client-side art only) ----
    Sprites sprites;
    // if (!atlas.loadDirectory("game/assets/art")) {
    //     std::cerr << "Failed to load sprites from game/assets/art\n";
    // }
    // TsxInfo tsx;
    // if (!load_tsx_info(tsxPath, tsx)) return 1;

    auto cover = LoadTexture("game/assets/img/cover.png");

    // Start with a local room until server sends one (optional)
    Room room;

    // Character: we only DRAW it now; position is authoritative from server STATE.
    Character guy(/*topTileId=*/0, /*startWorldPx=*/Vector2{16.0f * 5, 16.0f * 5});

    // Simple placeholder texture (in case not authed / no state yet)
    Image ph = GenImageChecked(16, 16, 4, 4, BLUE, SKYBLUE);
    Texture2D placeholder = LoadTextureFromImage(ph);
    UnloadImage(ph);

    Vector2 cam{0, 0};

    TextInput ui;
    ClientNet net;

    ui.pushLog("Commands:");
    ui.pushLog("  /connect <ip>");
    ui.pushLog("  /create <name> <pass>");
    ui.pushLog("  /identify <name> <pass>");
    ui.pushLog("  /logout");

    bool haveServerRoom = false;
    bool haveServerState = false;

    net.connectTo("127.0.0.1", 7777);
    net.sendLine("IDENTIFY ryan ryan1234");
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        (void)dt;

        // ---- Network: tick FIRST ----
        net.tick();

        // ---- UI ----
        ui.update(dt);

        if (auto cmd = ui.pollSubmitted()) {
            const std::string line = trim(*cmd);
            if (!line.empty()) {
                ui.pushLog("> " + line);

                std::istringstream iss(line);
                std::string c;
                iss >> c;

                if (c == "/connect") {
                    std::string ip;
                    iss >> ip;
                    if (ip.empty()) {
                        ui.pushLog("Usage: /connect <ip>");
                    } else {
                        ui.pushLog("Connecting to " + ip + ":7777 ...");
                        if (!net.connectTo(ip, 7777)) ui.pushLog("connectTo() failed");
                    }
                } else if (c == "/create") {
                    std::string name, pass;
                    iss >> name >> pass;
                    if (name.empty() || pass.empty()) ui.pushLog("Usage: /create <name> <pass>");
                    else if (!net.connected()) ui.pushLog("Not connected. Use /connect first.");
                    else net.sendLine("CREATE " + name + " " + pass);
                } else if (c == "/identify") {
                    std::string name, pass;
                    iss >> name >> pass;
                    if (name.empty() || pass.empty()) ui.pushLog("Usage: /identify <name> <pass>");
                    else if (!net.connected()) ui.pushLog("Not connected. Use /connect first.");
                    else net.sendLine("IDENTIFY " + name + " " + pass);
                } else if (c == "/logout") {
                    if (!net.connected()) ui.pushLog("Not connected.");
                    else net.sendLine("LOGOUT");
                } else {
                    ui.pushLog("Unknown command: " + c);
                }
            }
        }

        if (auto s = net.takeStatusLine()) ui.pushLog(*s);

        if (auto tmx = net.takeRoomTmx()) {
            // TMX arrived over network; baseDir is only for resolving TSX relative paths.
            // Your TMX uses ../assets/art/... so "game/map" makes it resolve to game/assets/art.
            if (room.loadFromXmlString(*tmx)) {
                ui.pushLog("Loaded room from server");
                haveServerRoom = true;
            } else {
                ui.pushLog("ERROR: could not parse server room; keeping current room");
            }
        }

        // Apply authoritative server position (this is what makes your guy move!)
        if (net.connected() && net.authed()) {
            guy.setGridPos(net.myX(), net.myY());
            haveServerState = true;
        }

        // ---- Input: send MOVE when authed ----
        if (!ui.isOpen() && net.connected() && net.authed()) {
            if (IsKeyPressed(KEY_A)) net.sendLine("MOVE L");
            if (IsKeyPressed(KEY_D)) net.sendLine("MOVE R");
            if (IsKeyPressed(KEY_W)) net.sendLine("MOVE U");
            if (IsKeyPressed(KEY_S)) net.sendLine("MOVE D");
        }
        guy.update(dt);

        // ---- Draw ----
        BeginDrawing();
        ClearBackground(RAYWHITE);


        // Other players (placeholder)
        if (net.connected() && net.authed()) {
            const float tile = 16.0f;
            for (const auto& p : net.others()) {
                float x = (float)p.x * tile - cam.x;
                float y = (float)p.y * tile - cam.y;
                DrawTextureEx(placeholder, Vector2{x, y}, 0.0f, 2.0f, WHITE);
                DrawText(p.name.c_str(), (int)x, (int)(y - 14), 12, DARKBLUE);
            }
        }


        // Me
        if (net.connected() && net.authed() && haveServerState) {
            guy.draw(Vector2{-cam.x, -cam.y});
        } else {
            // Not authed yet: draw a base image so you see *something*
            DrawText("Not logged in yet. /create or /identify", 20, 20, 18, DARKGRAY);
            DrawTextureEx(cover, Vector2{0,0}, 0.0f, 2.0f, WHITE);
        }

        if (haveServerRoom) DrawText("Room: server authoritative", 20, 44, 14, GRAY);

        ui.draw(GetScreenWidth(), GetScreenHeight());
        EndDrawing();
    }

    UnloadTexture(placeholder);
    CloseWindow();
    enet_deinitialize();
    return 0;
}

