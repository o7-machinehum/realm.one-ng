#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace voxel {

// On-disk and in-memory cube definition. Fixed size so the file can be
// memcpy'd straight into a struct array.
struct CubeDef {
    uint16_t id;            // 0 reserved for air; 1.. = cube types
    uint16_t hardness;      // mining/break time (arbitrary units)
    uint8_t  solid;         // 1 = blocks movement
    uint8_t  opaque;        // 1 = hides cubes behind it (renderer cull hint)
    uint8_t  light;         // emitted light level (0..15)
    uint8_t  reserved;
    uint16_t src_x;         // sub-rect within `texture` sheet
    uint16_t src_y;
    uint16_t src_w;
    uint16_t src_h;
    char     name[32];      // null-terminated identifier
    char     texture[64];   // path relative to game/assets, null-terminated
    char     material[16];  // free-form (e.g. "stone", "dirt")
    char     type[16];      // free-form (e.g. "surface", "underground", "water")
};
static_assert(sizeof(CubeDef) == 2+2+1+1+1+1+2+2+2+2+32+64+16+16, "CubeDef must be tightly packed");

struct WorldHeader {
    char     magic[4];      // "RVOX"
    uint32_t version;       // 1
    uint32_t size_x;
    uint32_t size_y;
    uint32_t size_z;        // max stack height
    uint32_t cube_def_count;
};
static_assert(sizeof(WorldHeader) == 4 + 5*4, "WorldHeader must be tightly packed");

constexpr char     kMagic[4]  = {'R', 'V', 'O', 'X'};
constexpr uint32_t kVersion   = 1;

// In-memory world. `voxels` is row-major: index(x,y,z) = z*size_y*size_x + y*size_x + x.
// A value of 0 means empty (air).
struct World {
    WorldHeader header{};
    std::vector<CubeDef> defs;
    std::vector<uint16_t> voxels;

    inline uint32_t index(uint32_t x, uint32_t y, uint32_t z) const {
        return z * header.size_y * header.size_x + y * header.size_x + x;
    }
    inline uint16_t at(uint32_t x, uint32_t y, uint32_t z) const {
        return voxels[index(x, y, z)];
    }
    inline void set(uint32_t x, uint32_t y, uint32_t z, uint16_t id) {
        voxels[index(x, y, z)] = id;
    }

    // Convenience: highest non-air z at (x,y), -1 if column is empty.
    int top_z(uint32_t x, uint32_t y) const;
};

// Allocates voxel storage to fit size_x * size_y * size_z, zeroed.
void resize(World& w, uint32_t size_x, uint32_t size_y, uint32_t size_z);

bool save(const World& w, const std::string& path, std::string* err = nullptr);
bool load(World& w, const std::string& path, std::string* err = nullptr);

} // namespace voxel
