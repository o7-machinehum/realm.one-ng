#include "voxel_world.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------- noise
uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float vnoise(float x, float y, uint32_t seed) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float xf = x - xi;
    float yf = y - yi;
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    float u = smooth(xf), v = smooth(yf);
    auto rnd = [&](int gx, int gy) {
        return (hash2(static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), seed) & 0xFFFFFF) / 16777215.0f;
    };
    float a = rnd(xi,     yi);
    float b = rnd(xi + 1, yi);
    float c = rnd(xi,     yi + 1);
    float d = rnd(xi + 1, yi + 1);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

float fbm(float x, float y, uint32_t seed) {
    float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < 4; ++i) {
        sum  += amp * vnoise(x * freq, y * freq, seed + i * 977u);
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

void setStr(char* dst, size_t cap, const char* src) {
    std::memset(dst, 0, cap);
    if (src) std::strncpy(dst, src, cap - 1);
}

// ---------------------------------------------------------------- TSX parsing
struct TsxTile {
    int tile_id = 0;        // index within the sheet
    std::string type;       // surface / underground / water / ...
};

struct TsxSheet {
    std::string image_rel;     // image source, relative to tsx file dir
    int tile_w = 0, tile_h = 0;
    int columns = 0;
    int img_w = 0, img_h = 0;
    std::vector<TsxTile> tiles;
};

bool loadTsx(const std::string& path, TsxSheet& out, std::string* err) {
    using namespace tinyxml2;
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) {
        if (err) *err = std::string("tsx load failed: ") + path;
        return false;
    }
    auto* ts = doc.FirstChildElement("tileset");
    if (!ts) { if (err) *err = "no <tileset>"; return false; }

    out.tile_w = ts->IntAttribute("tilewidth");
    out.tile_h = ts->IntAttribute("tileheight");
    out.columns = ts->IntAttribute("columns");

    auto* img = ts->FirstChildElement("image");
    if (img) {
        if (auto* s = img->Attribute("source")) out.image_rel = s;
        out.img_w = img->IntAttribute("width");
        out.img_h = img->IntAttribute("height");
    }

    for (auto* t = ts->FirstChildElement("tile"); t; t = t->NextSiblingElement("tile")) {
        TsxTile tile;
        tile.tile_id = t->IntAttribute("id");
        if (auto* props = t->FirstChildElement("properties")) {
            for (auto* p = props->FirstChildElement("property"); p; p = p->NextSiblingElement("property")) {
                const char* nm = p->Attribute("name");
                const char* vl = p->Attribute("value");
                if (nm && vl && std::strcmp(nm, "type") == 0) tile.type = vl;
            }
        }
        out.tiles.push_back(std::move(tile));
    }
    return true;
}

void usage() {
    std::printf(
        "worldgen --tsx PATH [--out PATH] [--size NxN] [--max-z N] [--seed N] [--sea-level N]\n"
        "  defaults: --tsx game/assets/cubes/tile1u.tsx --out data/world.dat\n"
        "            --size 64x64 --max-z 16 --seed 1337 --sea-level 5\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string tsx_path = "game/assets/cubes/tile1u.tsx";
    std::string out      = "data/world.dat";
    uint32_t sx = 64, sy = 64, sz = 16, seed = 1337;
    int sea_level = 5;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(1); }
            return argv[++i];
        };
        if      (a == "--tsx")       tsx_path = next();
        else if (a == "--out")       out = next();
        else if (a == "--seed")      seed = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--max-z")     sz = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--sea-level") sea_level = std::stoi(next());
        else if (a == "--size") {
            std::string v = next();
            auto x = v.find('x');
            if (x == std::string::npos) { usage(); return 1; }
            sx = static_cast<uint32_t>(std::stoul(v.substr(0, x)));
            sy = static_cast<uint32_t>(std::stoul(v.substr(x + 1)));
        } else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); usage(); return 1; }
    }

    TsxSheet sheet;
    std::string err;
    if (!loadTsx(tsx_path, sheet, &err)) {
        std::fprintf(stderr, "FATAL: %s\n", err.c_str());
        return 1;
    }
    // Texture path is the tsx's image source, relative to game/assets.
    namespace fs = std::filesystem;
    fs::path tsx_dir = fs::path(tsx_path).parent_path();
    fs::path img_full = tsx_dir / sheet.image_rel;
    fs::path texture_rel = fs::relative(img_full, fs::path("game/assets"));

    voxel::World w;
    voxel::resize(w, sx, sy, sz);

    // Build one CubeDef per tile that has a type property.
    // CubeDef.id starts at 1 (0 = air). Track which CubeDef is which type.
    int surface_id = 0, underground_id = 0, water_id = 0;
    uint16_t next_cube_id = 1;
    for (const auto& t : sheet.tiles) {
        if (t.type.empty()) continue;
        voxel::CubeDef def{};
        def.id       = next_cube_id++;
        def.hardness = 5;
        def.solid    = (t.type == "water") ? 0 : 1;
        def.opaque   = (t.type == "water") ? 0 : 1;
        def.light    = 0;
        const int col = (sheet.columns > 0) ? sheet.columns : 1;
        def.src_x = static_cast<uint16_t>((t.tile_id % col) * sheet.tile_w);
        def.src_y = static_cast<uint16_t>((t.tile_id / col) * sheet.tile_h);
        def.src_w = static_cast<uint16_t>(sheet.tile_w);
        def.src_h = static_cast<uint16_t>(sheet.tile_h);
        char namebuf[32];
        std::snprintf(namebuf, sizeof(namebuf), "%s_%d", t.type.c_str(), t.tile_id);
        setStr(def.name,     sizeof(def.name),     namebuf);
        setStr(def.texture,  sizeof(def.texture),  texture_rel.string().c_str());
        setStr(def.material, sizeof(def.material), t.type.c_str());
        setStr(def.type,     sizeof(def.type),     t.type.c_str());
        w.defs.push_back(def);

        // Remember a representative id per type for procgen.
        if (t.type == "surface"     && surface_id     == 0) surface_id     = def.id;
        if (t.type == "underground" && underground_id == 0) underground_id = def.id;
        if (t.type == "water"       && water_id       == 0) water_id       = def.id;
    }

    if (surface_id == 0 || underground_id == 0) {
        std::fprintf(stderr,
            "FATAL: tsx must define at least one 'surface' and one 'underground' tile\n");
        return 1;
    }

    // Heightmap: terrain elevation in [1, sz-1]. Surface on top, underground below.
    // Where terrain is below sea level, fill the gap with water.
    const float scale = 0.08f;
    for (uint32_t y = 0; y < sy; ++y) {
        for (uint32_t x = 0; x < sx; ++x) {
            float n = fbm(x * scale, y * scale, seed);
            int h = static_cast<int>(std::round(n * (sz - 1)));
            h = std::clamp(h, 1, static_cast<int>(sz) - 1);

            for (int z = 0; z < h - 1; ++z) {
                w.set(x, y, static_cast<uint32_t>(z), static_cast<uint16_t>(underground_id));
            }
            w.set(x, y, static_cast<uint32_t>(h - 1), static_cast<uint16_t>(surface_id));

            if (water_id != 0 && (h - 1) < sea_level) {
                for (int z = h; z <= sea_level && z < static_cast<int>(sz); ++z) {
                    w.set(x, y, static_cast<uint32_t>(z), static_cast<uint16_t>(water_id));
                }
            }
        }
    }

    if (!voxel::save(w, out, &err)) {
        std::fprintf(stderr, "save failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s  (%ux%ux%u, %zu cube defs from %s)\n",
                out.c_str(), sx, sy, sz, w.defs.size(), tsx_path.c_str());
    return 0;
}
