// Runtime state for a single spawned NPC instance on the server.
#pragma once

#include "combat_types.h"
#include "npc_defs.h"
#include "tile_pos.h"

#include <string>
#include <vector>

struct NpcRuntime {
    // ---- Identity ----
    int id = 0;
    std::string def_id;
    std::string name;

    // ---- Sprite / Display ----
    std::string sprite_tileset;
    std::string sprite_name;

    // ---- Position ----
    std::string room;          // Qualified room name this NPC is in.
    TilePos pos;               // Current tile position.
    TilePos home_pos;          // Home position; NPC won't wander beyond wander_radius from here.
    int size_w = 1;            // Sprite width in tiles.
    int size_h = 1;            // Sprite height in tiles.
    Facing facing = Facing::South;

    // ---- Movement ----
    int speed_ms = 700;        // Milliseconds between wander steps.
    int move_accum_ms = 0;     // Accumulated time since last movement.
    int talk_pause_ms = 0;     // Remaining pause time after being spoken to.
    int wander_radius = 3;     // Maximum Manhattan distance from home_pos.

    // ---- Dialogue ----
    std::vector<NpcDialogueDef> dialogues;
};
