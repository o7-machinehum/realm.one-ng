#pragma once

#include <string>
#include <vector>

struct ItemDef {
    std::string id;           // Derived from filename stem.
    std::string name;         // Display name.
    std::string sprite_tileset;
    std::string item_type;    // Weapon/Armor/Shield/Legs/Boots/Helmet
    int attack = 0;
    int defense = 0;
    int evasion = 0;
    bool stackable = false;
};

std::vector<ItemDef> loadItemDefs(const std::string& dir_path);
