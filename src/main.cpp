// src/main.cpp
#include <raylib.h>

#include <atomic>
#include <cctype>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "sprites.h"
#include "room.h"
#include "character.h"

// ------------------ stdin command queue ------------------
class CommandQueue {
public:
    void push(std::string cmd) {
        std::lock_guard<std::mutex> lk(m_);
        q_.push_back(std::move(cmd));
    }
    std::optional<std::string> try_pop() {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        auto cmd = std::move(q_.front());
        q_.pop_front();
        return cmd;
    }
private:
    std::mutex m_;
    std::deque<std::string> q_;
};

static std::string trim(std::string s) {
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && is_space((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_space((unsigned char)s.back())) s.pop_back();
    return s;
}

int main() {
    // ---- Terminal command thread setup ----
    CommandQueue cmdq;
    std::atomic<bool> running{true};

    std::thread stdinThread([&](){
        std::string line;
        while (running.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
            line = trim(line);
            if (!line.empty()) cmdq.push(line);
        }
    });

    // ---- Raylib window ----
    const int screenW = 960;
    const int screenH = 540;
    InitWindow(screenW, screenH, "quick_game");
    SetTargetFPS(60);

    // ---- Load sprites + room ----
    SpriteAtlas atlas;
    std::string status;

    if (!atlas.loadDirectory("game/assets/art")) {
        status = "Failed to load sprites from game/assets/art";
        std::cerr << status << "\n";
    } else {
        status = "Loaded sprites from game/assets/art";
    }

    Room room;
    if (!room.loadFromFile("game/map/d1.tmx")) {
        status = "Failed to load room game/map/d1.tmx";
        std::cerr << status << "\n";
    } else {
        status = "Loaded room game/map/d1.tmx";
    }

    // ---- Character ----
    // top tile id 0 is "F" in your character.tsx example.
    Character guy(/*topTileId=*/0, /*startWorldPx=*/Vector2{16.0f * 5, 16.0f * 5});

    // ---- Camera (world pixels) ----
    Vector2 cam{0, 0};

    auto handleCommand = [&](const std::string& cmdLine){
        std::istringstream iss(cmdLine);
        std::string cmd;
        iss >> cmd;

        if (cmd == "help") {
            status = "Commands: help | reload | cam <x> <y> | quit";
            std::cout << "Commands:\n"
                      << "  help\n"
                      << "  reload\n"
                      << "  cam <x> <y>\n"
                      << "  quit\n";
        } else if (cmd == "cam") {
            float x, y;
            if (iss >> x >> y) {
                cam = {x, y};
                status = "cam = (" + std::to_string((int)x) + "," + std::to_string((int)y) + ")";
            } else {
                status = "Usage: cam <x> <y>";
            }
        } else if (cmd == "reload") {
            // reload sprites + room (character reload is separate; keep it simple)
            atlas.loadDirectory("game/assets/art");
            if (room.loadFromFile("game/map/d1.tmx")) status = "Reloaded room + sprites";
            else status = "Reload failed (see stderr)";
        } else if (cmd == "quit" || cmd == "exit") {
            status = "Quitting...";
            running.store(false, std::memory_order_relaxed);
            CloseWindow();
        } else {
            status = "Unknown: " + cmdLine + " (try help)";
        }
    };

    // ---- Main loop ----
    while (!WindowShouldClose() && running.load(std::memory_order_relaxed)) {
        // terminal commands
        while (auto cmd = cmdq.try_pop()) handleCommand(*cmd);

        const float dt = GetFrameTime();

        // optional camera movement (arrow keys)
        const float camSpeed = 300.0f;
        if (IsKeyDown(KEY_RIGHT)) cam.x += camSpeed * dt;
        if (IsKeyDown(KEY_LEFT))  cam.x -= camSpeed * dt;
        if (IsKeyDown(KEY_DOWN))  cam.y += camSpeed * dt;
        if (IsKeyDown(KEY_UP))    cam.y -= camSpeed * dt;

        // character movement (WASD uses IsKeyPressed inside Character)
        guy.update(dt);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText(status.c_str(), 20, 20, 18, DARKGRAY);
        DrawText("terminal: help | reload | cam x y | quit", 20, 45, 18, GRAY);

        // NOTE:
        // If your Room::draw is already scaled 2x, and Character also scales 2x internally,
        // then they won't match. Pick ONE scaling approach.
        //
        // Current assumption:
        //  - Room::draw is NOT scaled internally (draws 1x world pixels)
        //  - Character draws scaled 2x internally (cfg.scale default 2.0)
        //
        // If you already scaled Room to 2x, set Character::Config{.scale=1.0f} when constructing guy.
        room.draw(atlas, Vector2{-cam.x, -cam.y});
        guy.draw(Vector2{-cam.x, -cam.y});

        EndDrawing();
    }

    // ---- Shutdown ----
    running.store(false, std::memory_order_relaxed);
    if (stdinThread.joinable()) stdinThread.join();

    if (IsWindowReady()) CloseWindow();
    return 0;
}

