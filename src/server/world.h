#pragma once

#include <string>
#include <map>
#include <algorithm>

#include "room.h"

// Container for all the rooms in the world
class World {
    std::map<std::string, std::unique_ptr<Room>> _world;
    std::string default_room_name_{"d1.tmx"};
public:
    World(std::string dir);
    const Room* getRoom(const std::string& name) const;
    const Room* defaultRoom() const;
};
