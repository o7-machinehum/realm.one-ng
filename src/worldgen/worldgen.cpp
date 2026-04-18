#include "voxel_world.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

// -------------------------------------------------------------- args
struct Args {
    std::string tsx_path  = "game/assets/cubes/tile1u.tsx";
    std::string out_path  = "data/world.dat";
    uint32_t    size_x    = 64;
    uint32_t    size_y    = 64;
    uint32_t    size_z    = 16;
    uint32_t    seed      = 1337;
    int         sea_level = 5;
};

void printUsage() {
    std::printf(
        "worldgen [--tsx PATH] [--out PATH] [--size NxN] [--max-z N] [--seed N] [--sea-level N]\n"
        "  defaults: --tsx game/assets/cubes/tile1u.tsx --out data/world.dat\n"
        "            --size 64x64 --max-z 16 --seed 1337 --sea-level 5\n");
}

bool parseSize(const std::string& s, uint32_t& w, uint32_t& h) {
    const auto x = s.find('x');
    if (x == std::string::npos) return false;
    try {
        w = static_cast<uint32_t>(std::stoul(s.substr(0, x)));
        h = static_cast<uint32_t>(std::stoul(s.substr(x + 1)));
        return true;
    } catch (...) { return false; }
}

std::optional<Args> parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto requireNext = [&]() -> std::string {
            if (i + 1 >= argc) { printUsage(); std::exit(1); }
            return argv[++i];
        };
        if      (a == "--tsx")        args.tsx_path  = requireNext();
        else if (a == "--out")        args.out_path  = requireNext();
        else if (a == "--seed")       args.seed      = static_cast<uint32_t>(std::stoul(requireNext()));
        else if (a == "--max-z")      args.size_z    = static_cast<uint32_t>(std::stoul(requireNext()));
        else if (a == "--sea-level")  args.sea_level = std::stoi(requireNext());
        else if (a == "--size") {
            if (!parseSize(requireNext(), args.size_x, args.size_y)) {
                printUsage(); return std::nullopt;
            }
        }
        else if (a == "--help" || a == "-h") { printUsage(); std::exit(0); }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); printUsage(); return std::nullopt; }
    }
    return args;
}

// -------------------------------------------------------------- noise
uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

float valueNoise(float x, float y, uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float u = smoothstep(x - xi);
    const float v = smoothstep(y - yi);
    auto rnd = [&](int gx, int gy) {
        return (hash2(static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), seed) & 0xFFFFFF) / 16777215.0f;
    };
    const float a = rnd(xi,     yi);
    const float b = rnd(xi + 1, yi);
    const float c = rnd(xi,     yi + 1);
    const float d = rnd(xi + 1, yi + 1);
    return (a * (1 - u) + b * u) * (1 - v)
         + (c * (1 - u) + d * u) * v;
}

float fractalBrownianMotion(float x, float y, uint32_t seed, int octaves = 4) {
    float amplitude = 1.0f, frequency = 1.0f, sum = 0.0f, normalizer = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum        += amplitude * valueNoise(x * frequency, y * frequency, seed + i * 977u);
        normalizer += amplitude;
        amplitude  *= 0.5f;
        frequency  *= 2.0f;
    }
    return sum / normalizer;
}

// -------------------------------------------------------------- tsx
struct TsxTile {
    int tile_id = 0;
    std::string type;
};

struct TsxSheet {
    std::string image_rel;
    int tile_w = 0;
    int tile_h = 0;
    int columns = 0;
    int img_w = 0;
    int img_h = 0;
    std::vector<TsxTile> tiles;
};

bool loadTileset(const std::string& path, TsxSheet& out, std::string* err) {
    using namespace tinyxml2;
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) {
        if (err) *err = "tsx load failed: " + path;
        return false;
    }
    auto* tileset = doc.FirstChildElement("tileset");
    if (!tileset) { if (err) *err = "no <tileset>"; return false; }

    out.tile_w  = tileset->IntAttribute("tilewidth");
    out.tile_h  = tileset->IntAttribute("tileheight");
    out.columns = tileset->IntAttribute("columns");

    if (auto* img = tileset->FirstChildElement("image")) {
        if (auto* s = img->Attribute("source")) out.image_rel = s;
        out.img_w = img->IntAttribute("width");
        out.img_h = img->IntAttribute("height");
    }

    for (auto* t = tileset->FirstChildElement("tile"); t; t = t->NextSiblingElement("tile")) {
        TsxTile tile;
        tile.tile_id = t->IntAttribute("id");
        if (auto* props = t->FirstChildElement("properties")) {
            for (auto* p = props->FirstChildElement("property"); p; p = p->NextSiblingElement("property")) {
                const char* name  = p->Attribute("name");
                const char* value = p->Attribute("value");
                if (name && value && std::strcmp(name, "type") == 0) tile.type = value;
            }
        }
        out.tiles.push_back(std::move(tile));
    }
    return true;
}

// -------------------------------------------------------------- cube defs
void copyString(char* dst, size_t cap, const char* src) {
    std::memset(dst, 0, cap);
    if (src) std::strncpy(dst, src, cap - 1);
}

std::string textureRelativeToAssets(const std::string& tsx_path, const std::string& image_rel) {
    namespace fs = std::filesystem;
    const fs::path tsx_dir = fs::path(tsx_path).parent_path();
    const fs::path img_full = tsx_dir / image_rel;
    return fs::relative(img_full, fs::path("game/assets")).string();
}

voxel::CubeDef makeCubeDef(uint16_t cube_id,
                           const TsxTile& tile,
                           const TsxSheet& sheet,
                           const std::string& texture_path) {
    voxel::CubeDef def{};
    def.id       = cube_id;
    def.hardness = 5;
    def.solid    = (tile.type == "water") ? 0 : 1;
    def.opaque   = (tile.type == "water") ? 0 : 1;
    def.light    = 0;
    const int columns = (sheet.columns > 0) ? sheet.columns : 1;
    def.src_x = static_cast<uint16_t>((tile.tile_id % columns) * sheet.tile_w);
    def.src_y = static_cast<uint16_t>((tile.tile_id / columns) * sheet.tile_h);
    def.src_w = static_cast<uint16_t>(sheet.tile_w);
    def.src_h = static_cast<uint16_t>(sheet.tile_h);

    char name_buf[32];
    std::snprintf(name_buf, sizeof(name_buf), "%s_%d", tile.type.c_str(), tile.tile_id);
    copyString(def.name,     sizeof(def.name),     name_buf);
    copyString(def.texture,  sizeof(def.texture),  texture_path.c_str());
    copyString(def.material, sizeof(def.material), tile.type.c_str());
    copyString(def.type,     sizeof(def.type),     tile.type.c_str());
    return def;
}

struct CubeDefIndex {
    std::vector<voxel::CubeDef> defs;
    uint16_t surface_id     = voxel::kAirCubeId;
    uint16_t underground_id = voxel::kAirCubeId;
    uint16_t water_id       = voxel::kAirCubeId;
};

CubeDefIndex buildCubeDefs(const TsxSheet& sheet, const std::string& texture_path) {
    CubeDefIndex idx;
    uint16_t next_id = 1;
    for (const auto& tile : sheet.tiles) {
        if (tile.type.empty()) continue;
        const auto def = makeCubeDef(next_id, tile, sheet, texture_path);
        idx.defs.push_back(def);
        if (tile.type == "surface"     && idx.surface_id     == voxel::kAirCubeId) idx.surface_id     = def.id;
        if (tile.type == "underground" && idx.underground_id == voxel::kAirCubeId) idx.underground_id = def.id;
        if (tile.type == "water"       && idx.water_id       == voxel::kAirCubeId) idx.water_id       = def.id;
        ++next_id;
    }
    return idx;
}

// -------------------------------------------------------------- terrain
constexpr float kTerrainNoiseScale = 0.08f;

void fillColumn(voxel::World& world, uint32_t x, uint32_t y,
                uint32_t terrain_height,
                const CubeDefIndex& defs, int sea_level) {
    for (uint32_t z = 0; z + 1 < terrain_height; ++z) {
        world.set(x, y, z, defs.underground_id);
    }
    if (terrain_height > 0) {
        world.set(x, y, terrain_height - 1, defs.surface_id);
    }
    if (defs.water_id == voxel::kAirCubeId) return;
    if (static_cast<int>(terrain_height) - 1 >= sea_level) return;
    for (int z = static_cast<int>(terrain_height); z <= sea_level; ++z) {
        if (z >= static_cast<int>(world.header.size_z)) break;
        world.set(x, y, static_cast<uint32_t>(z), defs.water_id);
    }
}

uint32_t terrainHeightAt(uint32_t x, uint32_t y, uint32_t seed, uint32_t max_z) {
    const float n = fractalBrownianMotion(x * kTerrainNoiseScale, y * kTerrainNoiseScale, seed);
    const int   h = static_cast<int>(std::round(n * (max_z - 1)));
    return static_cast<uint32_t>(std::clamp(h, 1, static_cast<int>(max_z) - 1));
}

void generateTerrain(voxel::World& world, const CubeDefIndex& defs,
                     uint32_t seed, int sea_level) {
    for (uint32_t y = 0; y < world.header.size_y; ++y) {
        for (uint32_t x = 0; x < world.header.size_x; ++x) {
            const uint32_t h = terrainHeightAt(x, y, seed, world.header.size_z);
            fillColumn(world, x, y, h, defs, sea_level);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto args = parseArgs(argc, argv);
    if (!args) return 1;

    TsxSheet sheet;
    {
        std::string err;
        if (!loadTileset(args->tsx_path, sheet, &err)) {
            std::fprintf(stderr, "FATAL: %s\n", err.c_str());
            return 1;
        }
    }

    const std::string texture_path = textureRelativeToAssets(args->tsx_path, sheet.image_rel);
    const auto defs = buildCubeDefs(sheet, texture_path);

    if (defs.surface_id == voxel::kAirCubeId || defs.underground_id == voxel::kAirCubeId) {
        std::fprintf(stderr,
            "FATAL: tsx must define at least one 'surface' and one 'underground' tile\n");
        return 1;
    }

    voxel::World world;
    voxel::resize(world, args->size_x, args->size_y, args->size_z);
    world.defs = defs.defs;

    generateTerrain(world, defs, args->seed, args->sea_level);

    std::string err;
    if (!voxel::save(world, args->out_path, &err)) {
        std::fprintf(stderr, "save failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s  (%ux%ux%u, %zu cube defs from %s)\n",
                args->out_path.c_str(),
                args->size_x, args->size_y, args->size_z,
                world.defs.size(), args->tsx_path.c_str());
    return 0;
}
