// Runtime state for a single spawned monster instance on the server.
#pragma once

#include "combat_types.h"
#include "monster_defs.h"

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
    std::string room;
    int x = 0;
    int y = 0;
    int spawn_x = 0;
    int spawn_y = 0;
    int size_w = 1;
    int size_h = 1;

    // ---- Combat ----
    int hp = 30;
    int max_hp = 30;
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
