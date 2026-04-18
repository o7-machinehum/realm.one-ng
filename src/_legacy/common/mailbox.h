// Thread-safe message queue and the Message variant used by client networking.
#pragma once

#include "msg_types.h"
#include "msg_structs.h"
#include "msg_game_state.h"
#include "room.h"

#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <variant>

using Message = std::variant<
    LoginMsg,
    ChatMsg,
    Room,
    LoginResultMsg,
    MoveMsg,
    RotateMsg,
    AttackMsg,
    PickupMsg,
    DropMsg,
    InventorySwapMsg,
    SetEquipmentMsg,
    MoveGroundItemMsg,
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
