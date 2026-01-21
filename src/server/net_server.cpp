#include "net_server.h"

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

static void printPeerIp(const ENetAddress& a) {
    char ip[64]{};
    enet_address_get_host_ip(&a, ip, sizeof(ip));
    std::cout << ip;
}

void NetServer::recvLoop() {
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port_;

    // max clients=16, channels=2, bandwidth limits=0 (unlimited)
    ENetHost* server = enet_host_create(&addr, 16, 2, 0, 0);

    std::cout << "[server] listening on port " << port_ << "\n";

    Room room;
    if (!room.loadFromFile("data/rooms/d1.tmx"));

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
                    Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);

                    switch (env.type) {
                        case MsgType::Login: {
                            LoginMsg m = fromBytes<LoginMsg>(env.payload.data(), env.payload.size());

                            std::cout << "[server] LOGIN user=" << m.user
                                      << " pass=" << m.pass << "\n";

                            auto wire = pack(MsgType::Room, room);
                            sendWire(ev.peer, /*channel*/ 0, wire);
                            enet_host_flush(server);
                        } break;

                        case MsgType::Chat: {
                            ChatMsg m = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());

                            std::cout << "[server] CHAT from=" << m.from
                                      << " text=" << m.text << "\n";

                            // Echo back
                            ChatMsg reply{"server", "echo: " + m.text};
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
}
