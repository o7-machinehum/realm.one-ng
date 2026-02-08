#pragma once
#include "raylib.h"
#include "room.h"

#include <filesystem>
#include <string>
#include <vector>

struct TsxInfo {
    int tileW = 0;
    int tileH = 0;
    int columns = 0;
    std::string image_source;
};

class RoomRenderer {
public:
    RoomRenderer() = default;
    ~RoomRenderer();

    RoomRenderer(const RoomRenderer&) = delete;
    RoomRenderer& operator=(const RoomRenderer&) = delete;

    bool load(const Room& room);     // load textures/tsx metadata for room tilesets
    void unload();

    void draw(const Room& room, float scale, Vector2 origin = {0,0}) const;

private:
    struct RuntimeTileset {
        int first_gid = 0;
        int tileW = 0;
        int tileH = 0;
        int columns = 0;
        bool drawable = true;
        Texture2D tex{};
    };

    std::vector<RuntimeTileset> sets_; // sorted by first_gid

    static bool load_tsx_info(const std::filesystem::path& tsx_path, TsxInfo& out);
    const RuntimeTileset* find_tileset(uint32_t gid) const;
};
