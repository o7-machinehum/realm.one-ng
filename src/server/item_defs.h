#pragma once

#include <string>
#include <vector>

struct ItemDef {
    std::string id;           // Derived from filename stem.
    std::string name;         // Display name.
    std::string sprite_tileset;
    std::string item_type;    // Weapon/Armor/Shield/Legs/Boots/Helmet
    bool stackable = false;
};

std::vector<ItemDef> loadItemDefs(const std::string& dir_path);
