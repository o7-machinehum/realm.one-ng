#include "voxel_world.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    std::string world_path = "data/world.dat";
    uint16_t port = 7000;
    if (argc >= 2) world_path = argv[1];
    if (argc >= 3) port = static_cast<uint16_t>(std::stoi(argv[2]));

    voxel::World world;
    std::string err;
    if (!voxel::load(world, world_path, &err)) {
        std::fprintf(stderr, "FATAL: failed to load %s: %s\n", world_path.c_str(), err.c_str());
        return 1;
    }
    std::printf("loaded %s  (%ux%ux%u, %zu cube defs)\n",
                world_path.c_str(),
                world.header.size_x, world.header.size_y, world.header.size_z,
                world.defs.size());
    std::printf("server stub idling on port %u (networking snubbed)\n", port);

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return 0;
}
