#pragma once

#include "room.h"

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <optional>
#include <mutex>
#include <map>
#include <deque>
#include <variant>

enum class MsgType : uint16_t {
    Login = 1,
    Chat,
    Room,
    LoginResult,
    Move,
    Attack,
    Pickup,
    Drop,
    InventorySwap,
    GameState
};

struct LoginMsg {
    std::string user;
    std::string pass;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(user, pass);
    }
};

struct ChatMsg {
    std::string from;
    std::string text;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, text);
    }
};

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

struct PickupMsg {
    int item_id = -1; // -1 means auto-pickup on current tile

    template <class Ar>
    void serialize(Ar& ar) {
        ar(item_id);
    }
};

struct DropMsg {
    int inventory_index = -1;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(inventory_index);
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

struct PlayerStateMsg {
    std::string user;
    std::string room;
    int x = 0;
    int y = 0;
    int exp = 0;
    int hp = 0;
    int max_hp = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(user, room, x, y, exp, hp, max_hp);
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

    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, name, sprite_tileset, sprite_name, sprite_w_tiles, sprite_h_tiles, room, x, y, hp, max_hp);
    }
};

struct GroundItemStateMsg {
    int id = 0;
    std::string name;
    std::string room;
    int x = 0;
    int y = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(id, name, room, x, y);
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
    int attack_target_monster_id = -1;
    std::vector<std::string> inventory;
    std::vector<PlayerStateMsg> players;
    std::vector<MonsterStateMsg> monsters;
    std::vector<GroundItemStateMsg> items;
    std::string event_text;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(your_user, your_room, your_x, your_y, your_exp, your_hp, your_max_hp,
           attack_target_monster_id, inventory, players, monsters, items, event_text);
    }
};

using Message = std::variant<
    LoginMsg,
    ChatMsg,
    Room,
    LoginResultMsg,
    MoveMsg,
    AttackMsg,
    PickupMsg,
    DropMsg,
    InventorySwapMsg,
    GameStateMsg
>;

class Mailbox {
public:
    void push(MsgType type, Message msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        inbox_[type].push_back(std::move(msg));
    }

    template<typename T>
    std::optional<T> pop(MsgType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inbox_.find(type);
        if (it == inbox_.end() || it->second.empty())
            return std::nullopt;

        auto& msg = it->second.front();

        // Ensure correct type is stored
        if (!std::holds_alternative<T>(msg))
            return std::nullopt;

        T result = std::get<T>(std::move(msg));
        it->second.pop_front();
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::map<MsgType, std::deque<Message>> inbox_;
};
