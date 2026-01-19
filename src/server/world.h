#pragma once

#include <string>
#include <map>
#include <algorithm>

#include "room.h"

// Container for all the rooms in the world
class World {
    std::map<std::string, std::unique_ptr<Room>> _world;
public:
    World(std::string dir);
};
