#include "sprites.h"

#include <tinyxml2.h>
#include <iostream>

static std::filesystem::path absPathNorm(const std::filesystem::path& p) {
    try {
        return std::filesystem::weakly_canonical(p);
    } catch (...) {
        return std::filesystem::absolute(p);
    }
}

static bool parseTsx(const std::filesystem::path& tsxPath, SpriteTileset& out) {
    tinyxml2::XMLDocument doc;
    auto rc = doc.LoadFile(tsxPath.string().c_str());
    if (rc != tinyxml2::XML_SUCCESS) {
        std::cerr << "TSX load failed: " << tsxPath << "\n";
        return false;
    }

    auto* tileset = doc.FirstChildElement("tileset");
    if (!tileset) return false;

    const char* nm = tileset->Attribute("name");
    out.name = nm ? nm : "";

    tileset->QueryIntAttribute("tilewidth", &out.tileW);
    tileset->QueryIntAttribute("tileheight", &out.tileH);
    tileset->QueryIntAttribute("columns", &out.columns);

    auto* img = tileset->FirstChildElement("image");
    if (!img) return false;

    const char* src = img->Attribute("source");
    if (!src) return false;

    auto tsxDir = tsxPath.parent_path();
    out.tsxPath = tsxPath;
    out.pngPath = absPathNorm(tsxDir / src);

    return true;
}

SpriteAtlas::~SpriteAtlas() {
    unloadAll();
}

void SpriteAtlas::unloadAll() {
    for (auto& kv : byTsxAbs_) {
        auto& ts = kv.second;
        if (ts.loaded) {
            UnloadTexture(ts.texture);
            ts.loaded = false;
        }
    }
    byTsxAbs_.clear();
}

bool SpriteAtlas::loadDirectory(const std::filesystem::path& artDir) {
    unloadAll();

    const auto base = absPathNorm(artDir);
    if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base)) {
        std::cerr << "artDir not found/dir: " << base << "\n";
        return false;
    }

    for (const auto& ent : std::filesystem::directory_iterator(base)) {
        if (!ent.is_regular_file()) continue;
        const auto p = ent.path();
        if (p.extension() != ".tsx") continue;

        SpriteTileset ts;
        const auto tsxAbs = absPathNorm(p);
        if (!parseTsx(tsxAbs, ts)) continue;

        // load texture
        ts.texture = LoadTexture(ts.pngPath.string().c_str());
        if (ts.texture.id == 0) {
            std::cerr << "PNG load failed for TSX: " << tsxAbs << " -> " << ts.pngPath << "\n";
            continue;
        }
        ts.loaded = true;

        byTsxAbs_[tsxAbs.string()] = std::move(ts);
    }

    return true;
}

const SpriteTileset* SpriteAtlas::findByTsxPath(const std::filesystem::path& tsxPath) const {
    const auto key = absPathNorm(tsxPath).string();
    auto it = byTsxAbs_.find(key);
    if (it == byTsxAbs_.end()) return nullptr;
    return &it->second;
}
