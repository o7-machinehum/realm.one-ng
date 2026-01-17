
// character.h
#pragma once

#include <raylib.h>
#include <filesystem>
#include <string>
#include <unordered_map>

class Character {
public:
    struct Config {
        std::filesystem::path tsxPath = "game/assets/art/character.tsx";
        float moveSeconds = 0.12f;
        float animSeconds = 0.12f;
        float scale = 2.0f;
    };

    // No default arg here
    Character(int topTileId, Vector2 startWorldPx, const Config& cfg);

    // Convenience overload (this is your “default config”)
    Character(int topTileId, Vector2 startWorldPx);

    ~Character();

    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

    void update(float dt);
    void draw(Vector2 originPx) const;
    void setGridPos(float x, float y) {
        gridX_ = x;
        gridY_ = y;
    }

    Vector2 worldPosPx() const { return worldPosPx_; }
    Vector2 gridPos() const { return Vector2{(float)gridX_, (float)gridY_}; }

private:
    struct DirEntry {
        int topId = 0;
        int bottomPx = 0;
    };

    bool loadFromTsx(const std::filesystem::path& tsxPath);
    void setDirection(char dir);
    void beginMove(int dx, int dy);
    void drawFrame(Vector2 originPx, int topId, int step) const;

    Texture2D tex_{};
    bool texLoaded_ = false;
    int tileW_ = 16;
    int tileH_ = 16;
    int columns_ = 17;

    std::unordered_map<char, DirEntry> dir_;

    Config cfg_;

    char facing_ = 'F';
    int baseTopId_ = 0;
    int bottomPx_ = 0;

    bool moving_ = false;
    float moveT_ = 0.0f;
    float animT_ = 0.0f;
    int animStep_ = 0;
    Vector2 moveFrom_{0,0};
    Vector2 moveTo_{0,0};

    int gridX_ = 0;
    int gridY_ = 0;
    Vector2 worldPosPx_{0,0};
};

