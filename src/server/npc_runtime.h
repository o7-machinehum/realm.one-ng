// Runtime state for a single spawned NPC instance on the server.
#pragma once

#include "combat_types.h"
#include "npc_defs.h"

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
    std::string room;
    int x = 0;
    int y = 0;
    int home_x = 0;
    int home_y = 0;
    int size_w = 1;
    int size_h = 1;
    Facing facing = Facing::South;

    // ---- Movement ----
    int speed_ms = 700;
    int move_accum_ms = 0;
    int talk_pause_ms = 0;
    int wander_radius = 3;

    // ---- Dialogue ----
    std::vector<NpcDialogueDef> dialogues;
};
