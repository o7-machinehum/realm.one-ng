#pragma once

#include <raylib.h>
#include <string>
#include <unordered_map>
#include <filesystem>

struct SpriteTileset {
    std::string name;                 // from TSX <tileset name="...">
    std::filesystem::path tsxPath;    // full path to .tsx
    std::filesystem::path pngPath;    // resolved from TSX <image source="...">
    int tileW = 0;
    int tileH = 0;
    int columns = 0;

    Texture2D texture{};
    bool loaded = false;
};

class SpriteAtlas {
public:
    SpriteAtlas() = default;
    ~SpriteAtlas();

    SpriteAtlas(const SpriteAtlas&) = delete;
    SpriteAtlas& operator=(const SpriteAtlas&) = delete;

    // Load ALL *.tsx under artDir (non-recursive). For each TSX, load referenced PNG.
    bool loadDirectory(const std::filesystem::path& artDir);

    // Lookup by TSX path (as referenced in TMX "source=...").
    // Pass a TMX-resolved absolute path for best results.
    const SpriteTileset* findByTsxPath(const std::filesystem::path& tsxPath) const;

    void unloadAll();

private:
    std::unordered_map<std::string, SpriteTileset> byTsxAbs_; // key = absolute tsx path string
};
