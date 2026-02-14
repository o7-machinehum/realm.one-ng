// Action messages sent from client to server (player intents).
#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <string>

struct LoginMsg {
    std::string user;
    std::string public_key_hex;
    std::string signature_hex;
    bool create_account = false;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(user, public_key_hex, signature_hex, create_account);
    }
};

struct ChatMsg {
    std::string from;
    std::string speech_type; // talk / think / yell
    std::string text;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, speech_type, text);
    }
};

struct MoveMsg {
    int dx = 0;
    int dy = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(dx, dy);
    }
};

struct AttackMsg {
    int target_monster_id = -1; // -1 clears active attack target

    template <class Ar>
    void serialize(Ar& ar) { ar(target_monster_id); }
};

struct RotateMsg {
    int dx = 0;
    int dy = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(dx, dy);
    }
};

struct PickupMsg {
    int item_id = -1; // -1 means auto-pickup on current tile

    template <class Ar>
    void serialize(Ar& ar) {
        ar(item_id);
    }
};

struct DropMsg {
    int inventory_index = -1;
    int to_x = -1; // -1 means drop at player's current tile
    int to_y = -1; // -1 means drop at player's current tile

    template <class Ar>
    void serialize(Ar& ar) {
        ar(inventory_index, to_x, to_y);
    }
};

struct InventorySwapMsg {
    int from_index = -1;
    int to_index = -1;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(from_index, to_index);
    }
};

struct MoveGroundItemMsg {
    int item_id = -1;
    int to_x = 0;
    int to_y = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(item_id, to_x, to_y);
    }
};

struct SetEquipmentMsg {
    std::string equip_type; // Weapon/Armor/Shield/Legs/Boots/Helmet
    int inventory_index = -1; // -1 means unequip this type

    template <class Ar>
    void serialize(Ar& ar) {
        ar(equip_type, inventory_index);
    }
};
