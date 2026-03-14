// Runtime state for a single item lying on the ground.
#pragma once

#include "tile_pos.h"

#include <cstdint>
#include <string>

struct GroundItemRuntime {
    int id = 0;              // session-scoped network ID for ground item references
    int64_t instance_id = 0; // unique item instance GID
    std::string item_id;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    int sprite_w_tiles = 1;
    int sprite_h_tiles = 1;
    int sprite_clip = 0; // 0 = Move, 1 = Death
    std::string room;
    TilePos pos;
};
