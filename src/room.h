#pragma once

#include <raylib.h>
#include <filesystem>
#include <string>
#include <vector>

#include <tinyxml2.h>

class SpriteAtlas;
struct SpriteTileset;

struct RoomLayer {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<int> gids; // CSV decoded, width*height
};

class Room {
public:
    Room() = default;

    // Load from file path (e.g. "game/map/d1.tmx")
    bool loadFromFile(const std::filesystem::path& tmxPath);

    // Load from raw XML string (e.g. received from server). baseDir is used to resolve relative tileset paths.
    bool loadFromXmlString(const std::string& tmxXml, const std::filesystem::path& baseDir);

    // Draw all layers using atlas. originPx is where tile(0,0) lands in screen coords (use for camera).
    void draw(const SpriteAtlas& atlas, Vector2 originPx) const;

    int mapWidth()  const { return mapW_; }
    int mapHeight() const { return mapH_; }
    int tileWidth() const { return tileW_; }
    int tileHeight() const { return tileH_; }

private:
    bool parseTmxDoc(tinyxml2::XMLDocument& doc, const std::filesystem::path& baseDir);

    int mapW_ = 0;
    int mapH_ = 0;
    int tileW_ = 16;
    int tileH_ = 16;

    int firstGid_ = 1;
    std::filesystem::path tsxAbsPath_; // resolved absolute TSX path

    std::vector<RoomLayer> layers_;
};
