#pragma once

#include <string>
#include <vector>

struct MonsterDropDef {
    std::string item_id;
    float chance = 0.0f; // 0.0 .. 1.0
};

struct MonsterDef {
    std::string id;            // Derived from filename stem.
    std::string name;          // Display name.
    std::string sprite_tileset;
    std::string sprite_name;
    int monster_size_w = 1;    // in 16px tiles
    int monster_size_h = 1;    // in 16px tiles
    int max_hp = 30;
    int strength = 6;
    int speed_ms = 500;
    int exp_reward = 10;
    std::vector<MonsterDropDef> drops;
};

std::vector<MonsterDef> loadMonsterDefs(const std::string& dir_path);
