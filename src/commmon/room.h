#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/unordered_map.hpp>

namespace tinyxml2 { class XMLDocument; }

struct RoomTilesetRef {
    int first_gid = 1;
    std::string source_tsx; // tsx fname

    template <class Ar>
    void serialize(Ar& ar) {
        ar(first_gid, source_tsx);
    }
};

struct RoomLayer {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> gids; // size = width*height

    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, width, height, gids);
    }
};

class Room {
public:
    bool loadFromFile(const std::filesystem::path& tmx_path);

    // ---- data access ----
    int map_width()  const { return map_w_; }
    int map_height() const { return map_h_; }
    int tile_width() const { return tile_w_; }
    int tile_height() const { return tile_h_; }

    const std::vector<RoomTilesetRef>& tilesets() const { return tilesets_; }
    const std::vector<RoomLayer>& layers() const { return layers_; }

    std::string get_name() {
        return name_;
    }

private:
    bool parseTmxDoc(tinyxml2::XMLDocument& doc);

    int map_w_ = 0;
    int map_h_ = 0;
    int tile_w_ = 16;
    int tile_h_ = 16;

    std::string name_; // Name of room inc. path
    std::vector<RoomTilesetRef> tilesets_;
    std::vector<RoomLayer> layers_;
    std::unordered_map<std::string, std::string> props_;

    friend class cereal::access;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(map_w_, map_h_, tile_w_, tile_h_, name_, tilesets_, layers_, props_);
    }
};
