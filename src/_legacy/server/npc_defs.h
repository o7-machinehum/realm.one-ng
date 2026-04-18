#pragma once

#include <string>
#include <vector>

struct NpcDialogueDef {
    std::vector<std::string> questions;
    std::string response;
};

struct NpcDef {
    std::string id;            // Derived from filename stem.
    std::string name;          // Display name.
    std::string sprite_tileset;
    int npc_size_w = 1;        // in 16px tiles
    int npc_size_h = 1;        // in 16px tiles
    int speed_ms = 700;
    int wander_radius = 3;     // in tiles, centered at spawn anchor
    std::vector<NpcDialogueDef> dialogues;
};

std::vector<NpcDef> loadNpcDefs(const std::string& dir_path);
