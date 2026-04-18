#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace voxel {

struct CubeDef {
    uint16_t id;
    uint16_t hardness;
    uint8_t  solid;
    uint8_t  opaque;
    uint8_t  light;
    uint8_t  reserved;
    uint16_t src_x;
    uint16_t src_y;
    uint16_t src_w;
    uint16_t src_h;
    char     name[32];
    char     texture[64];
    char     material[16];
    char     type[16];
};
static_assert(sizeof(CubeDef) == 2+2+1+1+1+1+2+2+2+2+32+64+16+16);

struct WorldHeader {
    char     magic[4];
    uint32_t version;
    uint32_t size_x;
    uint32_t size_y;
    uint32_t size_z;
    uint32_t cube_def_count;
};
static_assert(sizeof(WorldHeader) == 4 + 5*4);

constexpr char     kMagic[4] = {'R', 'V', 'O', 'X'};
constexpr uint32_t kVersion  = 1;

constexpr uint16_t kAirCubeId = 0;

// Heightmap-style voxel world. voxels is row-major:
//   index(x,y,z) = z*size_y*size_x + y*size_x + x
// A value of kAirCubeId means empty.
struct World {
    WorldHeader header{};
    std::vector<CubeDef> defs;
    std::vector<uint16_t> voxels;

    bool inBounds(int x, int y, int z) const;
    bool inBoundsXY(int x, int y) const;

    uint16_t at(uint32_t x, uint32_t y, uint32_t z) const;
    void     set(uint32_t x, uint32_t y, uint32_t z, uint16_t id);

    // Highest non-air z at (x, y); nullopt if column is empty or out of bounds.
    std::optional<uint32_t> topCubeZ(int x, int y) const;
};

void resize(World& w, uint32_t size_x, uint32_t size_y, uint32_t size_z);

bool save(const World& w, const std::string& path, std::string* err = nullptr);
bool load(World& w, const std::string& path, std::string* err = nullptr);

} // namespace voxel
