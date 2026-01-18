
#include "raylib.h"
#include "room.h"

#include <tinyxml2.h>

#include <filesystem>
#include <string>

using namespace tinyxml2;

struct TsxInfo {
    int tileW = 16;
    int tileH = 16;
    int columns = 0;
    std::string image_source;
};

static bool load_tsx_info(const std::filesystem::path& tsx_path, TsxInfo& out)
{
    XMLDocument doc;
    if (doc.LoadFile(tsx_path.string().c_str()) != XML_SUCCESS) return false;

    XMLElement* tileset = doc.FirstChildElement("tileset");
    if (!tileset) return false;

    tileset->QueryIntAttribute("tilewidth", &out.tileW);
    tileset->QueryIntAttribute("tileheight", &out.tileH);
    tileset->QueryIntAttribute("columns", &out.columns);

    XMLElement* img = tileset->FirstChildElement("image");
    if (!img) return false;

    const char* src = img->Attribute("source");
    if (!src) return false;

    out.image_source = src;
    return out.columns > 0 && out.tileW > 0 && out.tileH > 0;
}

int main()
{
    InitWindow(960, 640, "test_room: d1.tmx");
    SetTargetFPS(60);

    Room room;
    if (!room.loadFromFile("data/rooms/d1.tmx")) return 1;

    if (room.tilesets().empty()) return 1;

    // TMX path base (so we can resolve tileset .tsx and its image)
    const std::filesystem::path tmxPath = "data/rooms/d1.tmx";
    const std::filesystem::path tmxDir  = tmxPath.parent_path();

    // For MVP: use first tileset only
    const auto& tsref = room.tilesets()[0];
    const std::filesystem::path tsxPath = tmxDir / tsref.source_tsx;

    TsxInfo tsx;
    if (!load_tsx_info(tsxPath, tsx)) return 1;

    const std::filesystem::path texPath = tsxPath.parent_path() / tsx.image_source;
    Texture2D tex = LoadTexture(texPath.string().c_str());
    if (tex.id == 0) return 1;

    const float scale = 2.0f;

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(DARKGRAY);

        // optional grid
        for (int y = 0; y < GetScreenHeight(); y += (int)(room.tile_height() * scale))
            DrawLine(0, y, GetScreenWidth(), y, Fade(WHITE, 0.15f));
        for (int x = 0; x < GetScreenWidth(); x += (int)(room.tile_width() * scale))
            DrawLine(x, 0, x, GetScreenHeight(), Fade(WHITE, 0.15f));

        // draw all layers
        for (const auto& layer : room.layers()) {
            for (int y = 0; y < layer.height; y++) {
                for (int x = 0; x < layer.width; x++) {
                    uint32_t gid = layer.gids[y * layer.width + x];
                    if (gid == 0) continue;

                    // NOTE: flip flags ignored for MVP
                    int localId = (int)gid - tsref.first_gid;
                    if (localId < 0) continue;

                    int sx = (localId % tsx.columns) * tsx.tileW;
                    int sy = (localId / tsx.columns) * tsx.tileH;

                    Rectangle src{ (float)sx, (float)sy, (float)tsx.tileW, (float)tsx.tileH };
                    Rectangle dst{
                        (float)x * room.tile_width() * scale,
                        (float)y * room.tile_height() * scale,
                        (float)room.tile_width() * scale,
                        (float)room.tile_height() * scale
                    };

                    DrawTexturePro(tex, src, dst, Vector2{0,0}, 0.0f, WHITE);
                }
            }
        }

        DrawText("Rendering data/rooms/d1.tmx", 10, 10, 18, RAYWHITE);

        EndDrawing();
    }

    UnloadTexture(tex);
    CloseWindow();
    return 0;
}

