#include "room_render.h"
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

using namespace tinyxml2;

namespace {
std::string normalizedLayerName(const std::string& in) {
    size_t b = 0;
    size_t e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1])) != 0) --e;
    std::string out = in.substr(b, e - b);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool isMetadataLayerName(const std::string& name) {
    const std::string n = normalizedLayerName(name);
    return n == "monsters" || n == "items" || n == "block";
}

bool isUnderEntityLayerName(const std::string& name) {
    const std::string n = normalizedLayerName(name);
    if (n == "floor") return true; // legacy support
    if (n.size() < 2 || n[0] != 'l') return false;
    const char* num = n.c_str() + 1;
    char* end = nullptr;
    const long v = std::strtol(num, &end, 10);
    if (!end || *end != '\0') return false;
    return v >= 0 && v <= 5;
}

bool isAboveEntityLayerName(const std::string& name) {
    const std::string n = normalizedLayerName(name);
    if (n.size() < 2 || n[0] != 'l') return false;
    const char* num = n.c_str() + 1;
    char* end = nullptr;
    const long v = std::strtol(num, &end, 10);
    if (!end || *end != '\0') return false;
    return v >= 6;
}
} // namespace

RoomRenderer::~RoomRenderer() { unload(); }

void RoomRenderer::unload() {
    for (auto& s : sets_) {
        if (s.tex.id != 0) UnloadTexture(s.tex);
        s.tex = Texture2D{};
    }
    sets_.clear();
}

bool RoomRenderer::load_tsx_info(const std::filesystem::path& tsx_path, TsxInfo& out) {
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

bool RoomRenderer::load(const Room& room) {
    unload();

    if (room.tilesets().empty()) return false;

    sets_.reserve(room.tilesets().size());

    for (const auto& tsref : room.tilesets()) {
        RuntimeTileset rt;
        rt.first_gid = tsref.first_gid;
        rt.drawable = (tsref.source_tsx != "properties.tsx");

        const std::filesystem::path tsxPath =
            std::filesystem::path("game/assets/art/") / tsref.source_tsx;

        TsxInfo tsx;
        if (!load_tsx_info(tsxPath, tsx)) { unload(); return false; }

        const std::filesystem::path texPath = tsxPath.parent_path() / tsx.image_source;

        rt.tileW = tsx.tileW;
        rt.tileH = tsx.tileH;
        rt.columns = tsx.columns;
        rt.tex = LoadTexture(texPath.string().c_str());
        if (rt.tex.id == 0) { unload(); return false; }

        sets_.push_back(rt);
    }

    std::sort(sets_.begin(), sets_.end(),
              [](const RuntimeTileset& a, const RuntimeTileset& b) { return a.first_gid < b.first_gid; });

    return true;
}

const RoomRenderer::RuntimeTileset* RoomRenderer::find_tileset(uint32_t gid) const {
    // last tileset with first_gid <= gid
    const RuntimeTileset* best = nullptr;
    for (const auto& s : sets_) {
        if ((uint32_t)s.first_gid <= gid) best = &s;
        else break;
    }
    return best;
}

void RoomRenderer::drawLayer(const Room& room, const RoomLayer& layer, float scale, Vector2 origin) const {
    for (int y = 0; y < layer.height; y++) {
        for (int x = 0; x < layer.width; x++) {
            uint32_t gid = layer.gids[y * layer.width + x];
            if (gid == 0) continue;

            // NOTE: flip flags ignored for now
            const RuntimeTileset* ts = find_tileset(gid);
            if (!ts) continue;
            if (!ts->drawable) continue;

            int localId = (int)gid - ts->first_gid;
            if (localId < 0) continue;

            int sx = (localId % ts->columns) * ts->tileW;
            int sy = (localId / ts->columns) * ts->tileH;

            Rectangle src{ (float)sx, (float)sy, (float)ts->tileW, (float)ts->tileH };
            Rectangle dst{
                origin.x + (float)x * room.tile_width() * scale,
                origin.y + (float)y * room.tile_height() * scale,
                (float)room.tile_width() * scale,
                (float)room.tile_height() * scale
            };

            DrawTexturePro(ts->tex, src, dst, Vector2{0,0}, 0.0f, WHITE);
        }
    }
}

void RoomRenderer::drawUnderEntities(const Room& room, float scale, Vector2 origin) const {
    if (sets_.empty()) return;
    for (const auto& layer : room.layers()) {
        if (isMetadataLayerName(layer.name)) continue;
        if (!isUnderEntityLayerName(layer.name)) continue;
        drawLayer(room, layer, scale, origin);
    }
}

void RoomRenderer::drawAboveEntities(const Room& room, float scale, Vector2 origin) const {
    if (sets_.empty()) return;
    for (const auto& layer : room.layers()) {
        if (!isAboveEntityLayerName(layer.name)) continue;
        drawLayer(room, layer, scale, origin);
    }
}

void RoomRenderer::draw(const Room& room, float scale, Vector2 origin) const {
    drawUnderEntities(room, scale, origin);
    drawAboveEntities(room, scale, origin);
}
