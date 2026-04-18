#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

#include "tile_pos.h"

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

struct PortalTrigger {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string to_room;
    TilePos to_pos;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(x, y, w, h, to_room, to_pos.x, to_pos.y);
    }
};

struct MonsterSpawn {
    std::string monster_id;
    std::string name_override;
    TilePos pos;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(monster_id, name_override, pos.x, pos.y);
    }
};

struct ItemSpawn {
    std::string item_id;
    TilePos pos;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(item_id, pos.x, pos.y);
    }
};

struct NpcSpawn {
    std::string npc_id;
    TilePos pos;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(npc_id, pos.x, pos.y);
    }
};

// A single room (TMX map) with tile layers, portals, spawn data, and walkability.
class Room {
public:
    // Parses a TMX file, loading tilesets, layers, portals, spawns, and walkability.
    bool loadFromFile(const std::filesystem::path& tmx_path);

    // ---- data access ----
    int map_width()  const { return map_w_; }
    int map_height() const { return map_h_; }
    int tile_width() const { return tile_w_; }
    int tile_height() const { return tile_h_; }

    const std::vector<RoomTilesetRef>& tilesets() const { return tilesets_; }
    const std::vector<RoomLayer>& layers() const { return layers_; }
    const std::vector<PortalTrigger>& portals() const { return portals_; }
    const std::vector<MonsterSpawn>& monster_spawns() const { return monster_spawns_; }
    const std::vector<ItemSpawn>& item_spawns() const { return item_spawns_; }
    const std::vector<NpcSpawn>& npc_spawns() const { return npc_spawns_; }
    const std::unordered_map<std::string, std::string>& properties() const { return props_; }
    // Returns true if the tile at (x, y) is walkable (not blocked).
    bool isWalkable(int x, int y) const;

    std::string get_name() const {
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
    std::vector<PortalTrigger> portals_;
    std::vector<MonsterSpawn> monster_spawns_;
    std::vector<ItemSpawn> item_spawns_;
    std::vector<NpcSpawn> npc_spawns_;
    std::vector<uint8_t> walkable_mask_;
    std::unordered_map<std::string, std::string> props_;

    friend class cereal::access;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(map_w_, map_h_, tile_w_, tile_h_, name_, tilesets_, layers_, portals_, monster_spawns_, item_spawns_, npc_spawns_, props_);
    }
};
