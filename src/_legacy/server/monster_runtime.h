// Runtime state for a single spawned monster instance on the server.
#pragma once

#include "combat_types.h"
#include "monster_defs.h"
#include "tile_pos.h"

#include <cstdint>
#include <string>
#include <vector>

struct MonsterRuntime {
    // ---- Identity ----
    int id = 0;
    std::string def_id;
    std::string name;

    // ---- Sprite / Display ----
    std::string sprite_tileset;
    std::string sprite_name;

    // ---- Position ----
    std::string room;          // Qualified room name this monster is in.
    TilePos pos;               // Current tile position.
    TilePos spawn_pos;         // Original spawn position (used for respawning).
    int size_w = 1;            // Sprite width in tiles.
    int size_h = 1;            // Sprite height in tiles.

    // ---- Combat ----
    int hp = 30;               // Current hit points.
    int max_hp = 30;           // Maximum hit points.
    int strength = 6;
    int defense = 2;
    int evasion = 2;
    int accuracy = 60;
    int block_chance = 8;
    Facing facing = Facing::South;
    uint32_t attack_anim_seq = 0;
    CombatOutcome combat_outcome = CombatOutcome::None;
    uint32_t combat_outcome_seq = 0;
    int combat_value = 0;

    // ---- Movement ----
    int speed_ms = 500;
    int move_accum_ms = 0;

    // ---- Reward ----
    int exp_reward = 10;
    std::vector<MonsterDropDef> drops;
};
