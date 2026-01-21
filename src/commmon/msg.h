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
    Room
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

using Message = std::variant<LoginMsg, ChatMsg, Room>;

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
