#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include "msg.h"

// Envelope for holding a generic message
struct Envelope {
    MsgType type{};
    std::vector<uint8_t> payload;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(type, payload);
    }
};

template <class T>
static std::vector<uint8_t> toBytes(const T& obj) {
    std::ostringstream oss(std::ios::binary);
    cereal::BinaryOutputArchive oar(oss);
    oar(obj);

    const std::string s = oss.str();
    return {s.begin(), s.end()};
}

template <class T>
static T fromBytes(const uint8_t* data, size_t len) {
    std::string s(reinterpret_cast<const char*>(data), len);
    std::istringstream iss(s, std::ios::binary);
    cereal::BinaryInputArchive iar(iss);

    T obj{};
    iar(obj);
    return obj;
}

template <class T>
static std::vector<uint8_t> pack(MsgType type, const T& msg) {
    Envelope env{type, toBytes(msg)};
    return toBytes(env);
}
