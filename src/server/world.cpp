#include "world.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

World::World(std::string world_dir) {
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(world_dir)) {
        if (entry.is_regular_file()) {
            if (entry.path().extension() == ".tmx") {
                std::string fname{entry.path().string()};

                files.push_back(fname);
                std::cout << "Loading File: " << fname << std::endl;

                auto room = std::make_unique<Room>();
                room->loadFromFile(fname);
                _world[room->get_name()] = std::move(room);
            }
        }
    }

    if (!_world.empty() && _world.find(default_room_name_) == _world.end()) {
        default_room_name_ = _world.begin()->first;
    }
}

const Room* World::getRoom(const std::string& name) const {
    auto it = _world.find(name);
    if (it == _world.end()) return nullptr;
    return it->second.get();
}

const Room* World::defaultRoom() const {
    return getRoom(default_room_name_);
}
