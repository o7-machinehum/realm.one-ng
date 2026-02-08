#include <enet/enet.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "auth_db.h"
#include "msg.h"
#include "net_server.h"
#include "world.h"

int main() {
    if (enet_initialize() != 0) {
        std::cerr << "ERROR: enet_initialize failed\n";
        return 1;
    }

    World world("data/rooms/");
    Mailbox mailbox;

    try {
        AuthDb auth_db("data/game.db");
        NetServer ns(mailbox, world, auth_db, 7000);
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
