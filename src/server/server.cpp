#include <enet/enet.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <string>

#include "auth_db.h"
#include "msg.h"
#include "net_server.h"
#include "world.h"

int main(int argc, char** argv) {
    if (enet_initialize() != 0) {
        std::cerr << "ERROR: enet_initialize failed\n";
        return 1;
    }

    uint16_t port = 7000;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    World world("data/worlds");

    try {
        AuthDb auth_db("data/game.db");
        NetServer ns(world, auth_db, port);
        ns.start();

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    } catch (const std::exception& e) {
        std::cerr << "server startup failed: " << e.what() << "\n";
        enet_deinitialize();
        return 1;
    }

    enet_deinitialize();
    return 0;
}
