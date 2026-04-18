// State snapshot messages broadcast from server to clients.
#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "item_defs.h"

struct LoginResultMsg {
    bool ok = false;
    std::string message;
    std::string user;
    std::string room;
    int exp = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(ok, message, user, room, exp);
    }
};

struct EquippedItemMsg {
    ItemType equip_type;
    int64_t instance_id = 0;
    std::string item_name;
    std::string sprite_tileset;
    std::string sprite_name;
    std::string swing_type;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(equip_type, instance_id, item_name, sprite_tileset, sprite_name, swing_type);
    }
};

struct InventorySlotMsg {
    int64_t instance_id = 0;
    std::string def_id;
    std::string display_name;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(instance_id, def_id, display_name);
    }
};

struct PlayerStateMsg {
    std::string user;
    std::string room;
    int x = 0;
    int y = 0;
    int exp = 0;
    int hp = 0;
    int max_hp = 0;
    int mana = 0;
    int max_mana = 0;
    int facing = 2; // 0=N,1=E,2=S,3=W
    uint32_t attack_anim_seq = 0;
    int combat_outcome = 0; // 0 none, 1 hit, 2 missed, 3 blocked
    uint32_t combat_outcome_seq = 0;
    int combat_value = 0; // damage value when combat_outcome is hit
    std::vector<EquippedItemMsg> equipment;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(user, room, x, y, exp, hp, max_hp, mana, max_mana, facing, attack_anim_seq, combat_outcome, combat_outcome_seq, combat_value, equipment);
    }
};

struct MonsterStateMsg {
    int id = 0;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    int sprite_w_tiles = 1;
    int sprite_h_tiles = 1;
    std::string room;
    int x = 0;
    int y = 0;
    int hp = 0;
    int max_hp = 0;
    int facing = 2; // 0=N,1=E,2=S,3=W
    uint32_t attack_anim_seq = 0;
    int combat_outcome = 0; // 0 none, 1 hit, 2 missed, 3 blocked
    uint32_t combat_outcome_seq = 0;
    int combat_value = 0; // damage value when combat_outcome is hit

    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, name, sprite_tileset, sprite_name, sprite_w_tiles, sprite_h_tiles, room, x, y, hp, max_hp, facing, attack_anim_seq, combat_outcome, combat_outcome_seq, combat_value);
    }
};

struct GroundItemStateMsg {
    int id = 0;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    int sprite_w_tiles = 1;
    int sprite_h_tiles = 1;
    int sprite_clip = 0; // 0=Move, 1=Death
    std::string room;
    int x = 0;
    int y = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, name, sprite_tileset, sprite_name, sprite_w_tiles, sprite_h_tiles, sprite_clip, room, x, y);
    }
};

struct NpcStateMsg {
    int id = 0;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    int sprite_w_tiles = 1;
    int sprite_h_tiles = 1;
    std::string room;
    int x = 0;
    int y = 0;
    int facing = 2; // 0=N,1=E,2=S,3=W

    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, name, sprite_tileset, sprite_name, sprite_w_tiles, sprite_h_tiles, room, x, y, facing);
    }
};

struct GameStateMsg {
    std::string your_user;
    std::string your_room;
    int your_x = 0;
    int your_y = 0;
    int your_exp = 0;
    int your_hp = 0;
    int your_max_hp = 0;
    int your_mana = 0;
    int your_max_mana = 0;
    int your_level = 1;
    int your_exp_to_next_level = 100;
    int skill_melee_level = 1;
    int skill_melee_xp = 0;
    int skill_melee_xp_to_next = 100;
    int skill_distance_level = 1;
    int skill_distance_xp = 0;
    int skill_distance_xp_to_next = 100;
    int skill_magic_level = 1;
    int skill_magic_xp = 0;
    int skill_magic_xp_to_next = 100;
    int skill_shielding_level = 1;
    int skill_shielding_xp = 0;
    int skill_shielding_xp_to_next = 100;
    int skill_evasion_level = 1;
    int skill_evasion_xp = 0;
    int skill_evasion_xp_to_next = 100;
    int derived_defence = 1;
    int derived_offence = 1;
    int derived_evasion = 1;
    int trait_attack = 1;
    int trait_shielding = 1;
    int trait_evasion = 1;
    int trait_armor = 0;
    std::vector<EquippedItemMsg> your_equipment;
    int attack_target_monster_id = -1;
    uint32_t xp_gain_seq = 0;
    int xp_gain_amount = 0;
    std::vector<InventorySlotMsg> inventory;
    std::vector<PlayerStateMsg> players;
    std::vector<MonsterStateMsg> monsters;
    std::vector<NpcStateMsg> npcs;
    std::vector<GroundItemStateMsg> items;
    std::string event_text;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(your_user, your_room, your_x, your_y, your_exp, your_hp, your_max_hp, your_mana, your_max_mana,
           your_level, your_exp_to_next_level,
           skill_melee_level, skill_melee_xp, skill_melee_xp_to_next,
           skill_distance_level, skill_distance_xp, skill_distance_xp_to_next,
           skill_magic_level, skill_magic_xp, skill_magic_xp_to_next,
           skill_shielding_level, skill_shielding_xp, skill_shielding_xp_to_next,
           skill_evasion_level, skill_evasion_xp, skill_evasion_xp_to_next,
           derived_defence, derived_offence, derived_evasion,
           trait_attack, trait_shielding, trait_evasion, trait_armor,
           your_equipment, attack_target_monster_id, xp_gain_seq, xp_gain_amount, inventory, players, monsters, npcs, items, event_text);
    }
};
