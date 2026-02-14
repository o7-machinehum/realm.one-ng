#pragma once

#include "auth_db.h"
#include "monster_defs.h"
#include "npc_defs.h"

#include <enet/enet.h>

#include <cstdint>
#include <string>
#include <vector>

struct MonsterRuntime {
    int id = 0;
    std::string def_id;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    std::string room;
    int x = 0;
    int y = 0;
    int spawn_x = 0;
    int spawn_y = 0;
    int size_w = 1;
    int size_h = 1;
    int hp = 30;
    int max_hp = 30;
    int strength = 6;
    int defense = 2;
    int evasion = 2;
    int accuracy = 60;
    int block_chance = 8;
    int facing = 2; // South
    uint32_t attack_anim_seq = 0;
    int combat_outcome = 0; // 0 none, 1 hit, 2 missed, 3 blocked
    uint32_t combat_outcome_seq = 0;
    int combat_value = 0;
    int speed_ms = 500;
    int move_accum_ms = 0;
    int exp_reward = 10;
    std::vector<MonsterDropDef> drops;
};

struct GroundItemRuntime {
    int id = 0;
    std::string item_id;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    int sprite_w_tiles = 1;
    int sprite_h_tiles = 1;
    int sprite_clip = 0; // 0=Move, 1=Death
    std::string room;
    int x = 0;
    int y = 0;
};

struct NpcRuntime {
    int id = 0;
    std::string def_id;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    std::string room;
    int x = 0;
    int y = 0;
    int home_x = 0;
    int home_y = 0;
    int size_w = 1;
    int size_h = 1;
    int facing = 2; // South
    int speed_ms = 700;
    int move_accum_ms = 0;
    int talk_pause_ms = 0;
    int wander_radius = 3;
    std::vector<NpcDialogueDef> dialogues;
};

struct PlayerRuntime {
    ENetPeer* peer = nullptr;
    bool authenticated = false;
    PersistedPlayer data;
    int hp = 100;
    int max_hp = 100;
    int mana = 60;
    int max_mana = 60;
    int facing = 2; // South
    uint32_t attack_anim_seq = 0;
    int combat_outcome = 0; // 0 none, 1 hit, 2 missed, 3 blocked
    uint32_t combat_outcome_seq = 0;
    int combat_value = 0;
    int attack_target_monster_id = -1;
};
