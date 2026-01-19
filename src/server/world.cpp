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
}
