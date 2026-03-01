#pragma once

#include <string>
#include <map>
#include <algorithm>
#include <vector>
#include <optional>
#include <unordered_map>
#include <utility>

#include "room.h"
#include "tile_pos.h"

// Container for all the rooms in the world, handling multi-room world files,
// grid-based world positioning, room aliasing, and edge transitions.
class World {
    std::map<std::string, std::unique_ptr<Room>> _world;
    std::string default_room_name_{"0.tmx"};
    struct Placement {
        std::string world_name;
        std::string room_name;
        int tile_w = 16;
        int tile_h = 16;
        int map_w = 0;
        int map_h = 0;
    };
    std::map<std::string, Placement> placements_;
    std::unordered_map<std::string, std::string> first_room_alias_;
    std::unordered_map<std::string, std::pair<int, int>> world_grid_pos_;
    std::unordered_map<std::string, std::string> world_level0_room_;
    std::string default_world_name_{"floor"};

    // Scans a directory for .world files or raw .tmx files and loads them.
    void loadDirectory(const std::string& world_dir);
    // Parses a single .world JSON file, loading all referenced .tmx maps.
    void loadWorldFile(const std::string& world_file, const std::string& world_name);
    // Extracts an integer value from minimal JSON text by key name.
    static int findIntField(const std::string& text, const std::string& key, int fallback);
    // Extracts a string value from minimal JSON text by key name.
    static std::string findStringField(const std::string& text, const std::string& key);
    // Returns the single room name in a world, or nullopt if the world has multiple rooms.
    std::optional<std::string> onlyRoomInWorld(const std::string& world_name) const;
public:
    // Constructs a World from a directory path or a single .world file path.
    World(std::string source);
    // Looks up a room by qualified name, resolving aliases if needed.
    const Room* getRoom(const std::string& name) const;
    // Returns the default/fallback room.
    const Room* defaultRoom() const;
    // Returns all loaded room names (qualified, e.g. "500_500:0.tmx").
    std::vector<std::string> roomNames() const;
    // Resolves a raw room reference to its canonical qualified name.
    std::string resolveRoomName(const std::string& raw, const std::string& current_room) const;
    // Resolves stepping off the edge of a room into an adjacent world.
    // Returns true if the move is valid; fills out_room and out_pos.
    bool resolveEdgeTransition(const std::string& current_room,
                               TilePos attempted,
                               std::string& out_room,
                               TilePos& out_pos) const;
};
