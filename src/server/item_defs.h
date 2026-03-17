#pragma once

#include <string>
#include <vector>

enum ItemType {
    Weapon = 0,
    Armor,
    Shield,
    Legs,
    Boots,
    Helmet,
    Ring,
    Necklace,
};

enum ItemSubType {
    None,
    Sword,
};

// Parses a string (from TOML) into an ItemType enum value.
inline ItemType stringToItemType(const std::string& raw) {
    if (raw == "Weapon")   return ItemType::Weapon;
    if (raw == "Armor")    return ItemType::Armor;
    if (raw == "Shield")   return ItemType::Shield;
    if (raw == "Legs")     return ItemType::Legs;
    if (raw == "Boots")    return ItemType::Boots;
    if (raw == "Helmet")   return ItemType::Helmet;
    if (raw == "Ring")     return ItemType::Ring;
    if (raw == "Necklace") return ItemType::Necklace;
    return ItemType::Weapon;
}

struct ItemDef {
    std::string id;           // Derived from filename stem.
    std::string name;         // Display name.
    std::string sprite_tileset;
    ItemType item_type;
    ItemSubType item_subtype = ItemSubType::None;
    int attack = 0;
    int defense = 0;
    int evasion = 0;
    bool stackable = false;
    std::string swing_type;

    /* Can this item be equipt? */
    bool is_equipt_type() {
        if(item_type <= ItemType::Necklace)
            return true;
        return false;
    }
};

std::vector<ItemDef> loadItemDefs(const std::string& dir_path);
