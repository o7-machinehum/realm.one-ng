#include <enet/enet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "envelope.h"
#include "msg.h"
#include "room.h"
#include "room_render.h"

// Small wrapper around ENet packet creation + send.
// (Reliability is a packet flag; ENet will retransmit if needed.)
static void sendWire(ENetPeer* peer,
                     uint8_t channel,
                     const std::vector<uint8_t>& wire,
                     bool reliable = true) {
    ENetPacket* pkt = enet_packet_create(
        wire.data(),
        wire.size(),
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0
    );
    enet_peer_send(peer, channel, pkt);
}

// ============================================================================
// 5) tiny utilities
// ============================================================================

static void die(const char* msg) {
    std::cerr << "ERROR: " << msg << "\n";
    std::exit(1);
}

static void printPeerIp(const ENetAddress& a) {
    char ip[64]{};
    enet_address_get_host_ip(&a, ip, sizeof(ip));
    std::cout << ip;
}

// ============================================================================
// 6) Server: listens, receives Envelope, dispatches by MsgType, replies
// ============================================================================

static int runServer(uint16_t port) {
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port;

    // max clients=16, channels=2, bandwidth limits=0 (unlimited)
    ENetHost* server = enet_host_create(&addr, 16, 2, 0, 0);
    if (!server) die("enet_host_create(server) failed");

    std::cout << "[server] listening on port " << port << "\n";

    Room room;
    if (!room.loadFromFile("data/rooms/d1.tmx")) return 1;

    while (true) {
        ENetEvent ev{};
        // 100ms service tick (polling)
        while (enet_host_service(server, &ev, 100) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                std::cout << "[server] connect from ";
                printPeerIp(ev.peer->address);
                std::cout << "\n";
            }

            else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                try {
                    // Step 1: deserialize envelope
                    Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);

                    // Step 2: dispatch by message type
                    switch (env.type) {
                        case MsgType::Login: {
                            // Step 3: deserialize payload into the correct type
                            LoginMsg m = fromBytes<LoginMsg>(env.payload.data(), env.payload.size());

                            std::cout << "[server] LOGIN user=" << m.user
                                      << " pass=" << m.pass << "\n";

                            // Reply with a Chat message
                            // ChatMsg reply{"server", "welcome " + m.user};
                            auto wire = pack(MsgType::Room, room);
                            sendWire(ev.peer, /*channel*/ 0, wire);
                            enet_host_flush(server);
                        } break;

                        case MsgType::Chat: {
                            ChatMsg m = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());

                            std::cout << "[server] CHAT from=" << m.from
                                      << " text=" << m.text << "\n";

                            // Echo back
                            ChatMsg reply{"server", "talk", "echo: " + m.text};
                            auto wire = pack(MsgType::Chat, reply);
                            sendWire(ev.peer, /*channel*/ 0, wire);
                            enet_host_flush(server);
                        } break;

                        default:
                            std::cout << "[server] unknown message type\n";
                            break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[server] decode error: " << e.what() << "\n";
                }

                // ENet owns the packet; you must destroy it after processing.
                enet_packet_destroy(ev.packet);
            }

            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                std::cout << "[server] disconnect\n";
            }
        }
    }

    // unreachable, but shown for completeness
    enet_host_destroy(server);
    return 0;
}

// ============================================================================
// 7) Client: connects, sends Login + Chat, receives 2 Chat replies, exits
// ============================================================================

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
    RoomRenderer rr;
    BeginDrawing();
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
    EndDrawing();

    enet_peer_disconnect(peer, 0);
    enet_host_flush(client);
    enet_host_destroy(client);
    return 0;
}

// ============================================================================
// 8) main: pick server/client mode from argv
// ============================================================================

int main(int argc, char** argv) {
    if (enet_initialize() != 0) {
        std::cerr << "ERROR: enet_initialize failed\n";
        return 1;
    }

    int rc = 0;
    try {
        if (argc >= 3 && std::string(argv[1]) == "server") {
            rc = runServer(static_cast<uint16_t>(std::stoi(argv[2])));
        } else if (argc >= 4 && std::string(argv[1]) == "client") {
            rc = runClient(argv[2], static_cast<uint16_t>(std::stoi(argv[3])));
        } else {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " server <port>\n"
                      << "  " << argv[0] << " client <host> <port>\n";
            rc = 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        rc = 1;
    }

    enet_deinitialize();
    return rc;
}
