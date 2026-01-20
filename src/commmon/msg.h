#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

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
