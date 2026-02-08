#pragma once

#include <string>
#include <vector>

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
};

std::vector<MonsterDef> loadMonsterDefs(const std::string& dir_path);
