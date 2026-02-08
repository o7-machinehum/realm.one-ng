#pragma once

#include <string>
#include <map>
#include <algorithm>
#include <vector>
#include <optional>
#include <unordered_map>

#include "room.h"

// Container for all the rooms in the world
class World {
    std::map<std::string, std::unique_ptr<Room>> _world;
    std::string default_room_name_{"d1.tmx"};
    struct Placement {
        std::string world_name;
        std::string room_name;
        int world_x = 0;
        int world_y = 0;
        int pixel_w = 0;
        int pixel_h = 0;
        int tile_w = 16;
        int tile_h = 16;
        int map_w = 0;
        int map_h = 0;
    };
    std::map<std::string, Placement> placements_;
    std::unordered_map<std::string, std::string> first_room_alias_;
    std::string default_world_name_{"floor"};

    void loadDirectory(const std::string& world_dir);
    void loadWorldFile(const std::string& world_file, const std::string& world_name);
    static int findIntField(const std::string& text, const std::string& key, int fallback);
    static std::string findStringField(const std::string& text, const std::string& key);
    std::optional<std::string> onlyRoomInWorld(const std::string& world_name) const;
public:
    World(std::string source);
    const Room* getRoom(const std::string& name) const;
    const Room* defaultRoom() const;
    std::vector<std::string> roomNames() const;
    std::string resolveRoomName(const std::string& raw, const std::string& current_room) const;
    bool resolveEdgeTransition(const std::string& current_room,
                               int attempted_x,
                               int attempted_y,
                               std::string& out_room,
                               int& out_x,
                               int& out_y) const;
};
