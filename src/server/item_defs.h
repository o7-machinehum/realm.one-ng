#pragma once

#include <string>
#include <vector>

struct ItemDef {
    std::string id;           // Derived from filename stem.
    std::string name;         // Display name.
    std::string sprite_tileset;
    std::string sprite_name;
    bool stackable = false;
};

std::vector<ItemDef> loadItemDefs(const std::string& dir_path);
