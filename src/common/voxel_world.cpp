#include "voxel_world.h"

#include <cstdio>
#include <cstring>

namespace voxel {

int World::top_z(uint32_t x, uint32_t y) const {
    for (int z = static_cast<int>(header.size_z) - 1; z >= 0; --z) {
        if (at(x, y, static_cast<uint32_t>(z)) != 0) return z;
    }
    return -1;
}

void resize(World& w, uint32_t size_x, uint32_t size_y, uint32_t size_z) {
    w.header.size_x = size_x;
    w.header.size_y = size_y;
    w.header.size_z = size_z;
    w.voxels.assign(static_cast<size_t>(size_x) * size_y * size_z, 0);
}

static void setError(std::string* err, const char* msg) {
    if (err) *err = msg;
}

bool save(const World& w, const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { setError(err, "cannot open file for write"); return false; }

    WorldHeader h = w.header;
    std::memcpy(h.magic, kMagic, 4);
    h.version = kVersion;
    h.cube_def_count = static_cast<uint32_t>(w.defs.size());

    bool ok = true;
    if (std::fwrite(&h, sizeof(h), 1, f) != 1) ok = false;
    if (ok && !w.defs.empty() &&
        std::fwrite(w.defs.data(), sizeof(CubeDef), w.defs.size(), f) != w.defs.size()) ok = false;
    if (ok && !w.voxels.empty() &&
        std::fwrite(w.voxels.data(), sizeof(uint16_t), w.voxels.size(), f) != w.voxels.size()) ok = false;

    std::fclose(f);
    if (!ok) setError(err, "short write");
    return ok;
}

bool load(World& w, const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { setError(err, "cannot open file for read"); return false; }

    if (std::fread(&w.header, sizeof(WorldHeader), 1, f) != 1) {
        std::fclose(f); setError(err, "short header"); return false;
    }
    if (std::memcmp(w.header.magic, kMagic, 4) != 0) {
        std::fclose(f); setError(err, "bad magic"); return false;
    }
    if (w.header.version != kVersion) {
        std::fclose(f); setError(err, "unsupported version"); return false;
    }

    w.defs.resize(w.header.cube_def_count);
    if (w.header.cube_def_count > 0 &&
        std::fread(w.defs.data(), sizeof(CubeDef), w.defs.size(), f) != w.defs.size()) {
        std::fclose(f); setError(err, "short defs"); return false;
    }

    const size_t voxel_count =
        static_cast<size_t>(w.header.size_x) * w.header.size_y * w.header.size_z;
    w.voxels.resize(voxel_count);
    if (voxel_count > 0 &&
        std::fread(w.voxels.data(), sizeof(uint16_t), voxel_count, f) != voxel_count) {
        std::fclose(f); setError(err, "short voxels"); return false;
    }

    std::fclose(f);
    return true;
}

} // namespace voxel
