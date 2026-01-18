#include <enet/enet.h>
#include <iostream>
#include <thread>
#include <chrono>
#include "server_net.h"
#include "fs_db.h"

int main(int argc, char** argv) {
    if (enet_initialize() != 0) {
        std::cerr << "enet_initialize failed\n";
        return 1;
    }

    int port = 7777;
    if (argc >= 2) port = std::stoi(argv[1]);

    FsDb db("data");
    ServerNet server(port, std::move(db));
    if (!server.start()) return 2;

    // basic tick loop
    double last = enet_time_get() / 1000.0;
    while (true) {
        double now = enet_time_get() / 1000.0;
        float dt = (float)(now - last);
        last = now;

        server.tick(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    enet_deinitialize();
    return 0;
}
