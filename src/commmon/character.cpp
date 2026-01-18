#include "character.h"

#include <tinyxml2.h>
#include <iostream>

static std::filesystem::path absPathNorm(const std::filesystem::path& p) {
    try { return std::filesystem::weakly_canonical(p); }
    catch (...) { return std::filesystem::absolute(p); }
}

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

Character::Character(int topTileId, Vector2 startWorldPx)
    : Character(topTileId, startWorldPx, Config{}) {}
Character::Character(int topTileId, Vector2 startWorldPx, const Config& cfg)
    : cfg_(cfg)
{
    if (!loadFromTsx(cfg_.tsxPath)) {
        std::cerr << "Character: failed to load TSX: " << cfg_.tsxPath << "\n";
        return;
    }

    // init position
    worldPosPx_ = startWorldPx;
    gridX_ = (int)(startWorldPx.x / 16.0f);
    gridY_ = (int)(startWorldPx.y / 16.0f);

    // pick facing based on provided topTileId if it matches known dirs
    char initialDir = 'F';
    for (auto& kv : dir_) {
        if (kv.second.topId == topTileId) {
            initialDir = kv.first;
            break;
        }
    }
    setDirection(initialDir);
}

Character::~Character() {
    if (texLoaded_) {
        UnloadTexture(tex_);
        texLoaded_ = false;
    }
}

bool Character::loadFromTsx(const std::filesystem::path& tsxPathIn) {
    auto tsxAbs = absPathNorm(tsxPathIn);

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(tsxAbs.string().c_str()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "TSX load failed: " << tsxAbs << "\n";
        return false;
    }

    auto* tileset = doc.FirstChildElement("tileset");
    if (!tileset) return false;

    tileset->QueryIntAttribute("tilewidth", &tileW_);
    tileset->QueryIntAttribute("tileheight", &tileH_);
    tileset->QueryIntAttribute("columns", &columns_);

    auto* img = tileset->FirstChildElement("image");
    if (!img) return false;

    const char* src = img->Attribute("source");
    if (!src) return false;

    auto pngAbs = absPathNorm(tsxAbs.parent_path() / src);

    tex_ = LoadTexture(pngAbs.string().c_str());
    if (tex_.id == 0) {
        std::cerr << "PNG load failed: " << pngAbs << "\n";
        return false;
    }
    texLoaded_ = true;
    SetTextureFilter(tex_, TEXTURE_FILTER_POINT);

    // Parse direction entries from tiles with properties: bottom (int), dir (string)
    dir_.clear();
    for (auto* tile = tileset->FirstChildElement("tile"); tile; tile = tile->NextSiblingElement("tile")) {
        int id = 0;
        tile->QueryIntAttribute("id", &id);

        auto* props = tile->FirstChildElement("properties");
        if (!props) continue;

        int bottom = -1;
        std::string dirStr;

        for (auto* prop = props->FirstChildElement("property"); prop; prop = prop->NextSiblingElement("property")) {
            const char* name = prop->Attribute("name");
            if (!name) continue;

            if (std::string(name) == "bottom") {
                prop->QueryIntAttribute("value", &bottom);
            } else if (std::string(name) == "dir") {
                const char* v = prop->Attribute("value");
                if (v) dirStr = v;
            }
        }

        if (bottom >= 0 && dirStr.size() == 1) {
            char d = dirStr[0];
            if (d == 'F' || d == 'R' || d == 'B' || d == 'L') {
                dir_[d] = DirEntry{ .topId = id, .bottomPx = bottom };
            }
        }
    }

    // Require at least F entry to exist
    if (dir_.find('F') == dir_.end()) {
        std::cerr << "TSX missing required tile properties for dir='F'\n";
        return false;
    }

    return true;
}

void Character::setDirection(char dir) {
    auto it = dir_.find(dir);
    if (it == dir_.end()) return;

    facing_ = dir;
    baseTopId_ = it->second.topId;
    bottomPx_ = it->second.bottomPx;

    // reset walk cycle when turning (optional, feels nicer)
    animStep_ = 0;
    animT_ = 0.0f;
}

void Character::beginMove(int dx, int dy) {
    if (moving_) return;

    // update facing immediately on input
    if (dx == 1) setDirection('R');
    else if (dx == -1) setDirection('L');
    else if (dy == 1) setDirection('F');  // assuming screen-down is "front"
    else if (dy == -1) setDirection('B');

    moveFrom_ = worldPosPx_;
    moveTo_   = Vector2{ worldPosPx_.x + dx * (float)tileW_, worldPosPx_.y + dy * (float)tileH_ };

    moving_ = true;
    moveT_ = 0.0f;

    // start anim
    animT_ = 0.0f;
    animStep_ = 0;
}

void Character::update(float dt) {
    // Input only when not already moving (grid step per keypress)
    if (!moving_) {
        int dx = 0, dy = 0;

        if (IsKeyPressed(KEY_D)) dx = 1;
        else if (IsKeyPressed(KEY_A)) dx = -1;
        else if (IsKeyPressed(KEY_S)) dy = 1;
        else if (IsKeyPressed(KEY_W)) dy = -1;

        if (dx || dy) beginMove(dx, dy);
    }

    if (moving_) {
        // slide progress
        const float moveDur = (cfg_.moveSeconds <= 0.001f) ? 0.001f : cfg_.moveSeconds;
        moveT_ += dt / moveDur;
        if (moveT_ > 1.0f) moveT_ = 1.0f;

        worldPosPx_.x = moveFrom_.x + (moveTo_.x - moveFrom_.x) * moveT_;
        worldPosPx_.y = moveFrom_.y + (moveTo_.y - moveFrom_.y) * moveT_;

        // 4-step walk cycle across the move
        // step = 0..3 while moving. When finished, go back to 0.
        const float animDur = (cfg_.animSeconds <= 0.001f) ? moveDur : cfg_.animSeconds;
        animT_ += dt / animDur;
        if (animT_ > 1.0f) animT_ = 1.0f;

        animStep_ = clampi((int)(animT_ * 4.0f), 0, 3);

        if (moveT_ >= 1.0f) {
            moving_ = false;
            worldPosPx_ = moveTo_;

            gridX_ = (int)(worldPosPx_.x / (float)tileW_);
            gridY_ = (int)(worldPosPx_.y / (float)tileH_);

            animStep_ = 0;
            animT_ = 0.0f;
        }
    }
}

void Character::drawFrame(Vector2 originPx, int topId, int step) const {
    if (!texLoaded_) return;

    const float s = cfg_.scale;

    // top tile local id (tileset index) => source rect
    const int topLocal = topId + step;
    const int topSx = (topLocal % columns_) * tileW_;
    const int topSy = (topLocal / columns_) * tileH_;

    Rectangle srcTop{ (float)topSx, (float)topSy, (float)tileW_, (float)tileH_ };

    // bottom tile: 1 tile row below in the sheet (2-tile tall sprite)
    const int bottomLocal = topLocal + columns_;
    const int botSx = (bottomLocal % columns_) * tileW_;
    const int botSy = (bottomLocal / columns_) * tileH_;

    Rectangle srcBot{ (float)botSx, (float)botSy, (float)tileW_, (float)tileH_ };

    // destination: stack 2 tiles vertically in world (top at worldPosPx)
    Rectangle dstTop{
        originPx.x + worldPosPx_.x * s,
        originPx.y + worldPosPx_.y * s,
        (float)tileW_ * s,
        (float)tileH_ * s
    };

    Rectangle dstBot{
        dstTop.x,
        dstTop.y + (float)tileH_ * s,
        (float)tileW_ * s,
        (float)tileH_ * s
    };

    DrawTexturePro(tex_, srcTop, dstTop, Vector2{0,0}, 0.0f, WHITE);
    DrawTexturePro(tex_, srcBot, dstBot, Vector2{0,0}, 0.0f, WHITE);
}

void Character::draw(Vector2 originPx) const {
    // originPx is in SCREEN space already; we scale world pixels by cfg_.scale
    // If you want to keep room+character consistent, also scale room draw similarly.
    drawFrame(originPx, baseTopId_, moving_ ? animStep_ : 0);
}
