#include <enet/enet.h>
#include <iostream>
#include <thread>
#include <chrono>

#include "net_server.h"
#include "world.h"
#include "msg.h"

int main() {
    World world("data/rooms/"); // Load the entire world
    Mailbox mailbox;
    NetServer ns(mailbox, 7000);
    ns.start();

    while (true) {
        // if(auto login = mailbox.pop<LoginMsg>(MsgType::Login)) {
        //     // Verify
        // }
    }

    enet_deinitialize();
    return 0;
}
