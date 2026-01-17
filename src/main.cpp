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
#include "text_input.h"

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

    // ---- Raylib window ----
    const int screenW = 960;
    const int screenH = 905;
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

    TextInput ui;

    // ---- Main loop ----
    while (!WindowShouldClose() && running.load(std::memory_order_relaxed)) {
        ui.update(GetFrameTime());
        if (auto cmd = ui.pollSubmitted()) {
            std::cout << *cmd << std::endl;
        }

        const float dt = GetFrameTime();

        // character movement (WASD uses IsKeyPressed inside Character)
        if (!ui.isOpen()) guy.update(dt);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        room.draw(atlas, Vector2{-cam.x, -cam.y});
        guy.draw(Vector2{-cam.x, -cam.y});
        ui.draw(GetScreenWidth(), GetScreenHeight());

        EndDrawing();
    }

    // ---- Shutdown ----
    running.store(false, std::memory_order_relaxed);

    if (IsWindowReady()) CloseWindow();
    return 0;
}

