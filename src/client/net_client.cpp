#include "net_client.h"

static int runClient(const char* hostName, uint16_t port) {
    ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!client) die("enet_host_create(client) failed");

    ENetAddress addr{};
    if (enet_address_set_host(&addr, hostName) != 0) die("enet_address_set_host failed");
    addr.port = port;

    ENetPeer* peer = enet_host_connect(client, &addr, 2, 0);
    if (!peer) die("enet_host_connect failed");

    std::cout << "[client] connecting to " << hostName << ":" << port << "...\n";

    // Wait for connect (3s timeout)
    {
        ENetEvent ev{};
        if (enet_host_service(client, &ev, 3000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
            enet_peer_reset(peer);
            die("connect timeout");
        }
    }

    std::cout << "[client] connected\n";

    // Send a Login message
    {
        LoginMsg login{"Ryan", "hunter2"};
        auto wire = pack(MsgType::Login, login);
        sendWire(peer, /*channel*/ 0, wire);
    }

    // Flush pushes queued packets out immediately.
    enet_host_flush(client);

    // Receive two chat replies, then exit.
    int replies = 0;
    while (replies < 2) {
        ENetEvent ev{};
        if (enet_host_service(client, &ev, 3000) <= 0) die("timeout waiting for replies");

        if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
            try {
                Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);

                if (env.type == MsgType::Room) {
                    Room r = fromBytes<Room>(env.payload.data(), env.payload.size());
                    ClearBackground(DARKGRAY);
                    rr.draw(r, 2.0f);
                } else {
                    std::cout << "[client] got non-chat message\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "[client] decode error: " << e.what() << "\n";
            }

            enet_packet_destroy(ev.packet);
        } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            die("disconnected early");
        }

    }

    enet_peer_disconnect(peer, 0);
    enet_host_flush(client);
    enet_host_destroy(client);
    return 0;
}
