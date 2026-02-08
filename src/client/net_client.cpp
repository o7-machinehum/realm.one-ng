#include <iostream>
#include <sstream>

#include "net_client.h"
#include "msg.h"
#include "envelope.h"

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

static void die(const char* msg) {
    std::cerr << "ERROR: " << msg << "\n";
    std::exit(1);
}

void NetClient::recvLoop() {
    ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!client) die("enet_host_create(client) failed");

    ENetAddress addr{};
    if (enet_address_set_host(&addr, hostname_.c_str()) != 0) die("enet_address_set_host failed");
    addr.port = port_;

    ENetPeer* peer = enet_host_connect(client, &addr, 2, 0);
    if (!peer) die("enet_host_connect failed");

    std::cout << "[client] connecting to " << hostname_ << ":" << port_ << "...\n";

    // Wait for connect (3s timeout)
    {
        ENetEvent ev{};
        if (enet_host_service(client, &ev, 3000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
            enet_peer_reset(peer);
            die("connect timeout");
        }
    }

    std::cout << "[client] connected\n";
    enet_host_flush(client);

    while (running_) {
        // Check mailbox for outgoing Login message
        if (auto login = mailbox_.pop<LoginMsg>(MsgType::Login)) {
            auto wire = pack(MsgType::Login, *login);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }
        if (auto move = mailbox_.pop<MoveMsg>(MsgType::Move)) {
            auto wire = pack(MsgType::Move, *move);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }
        if (auto atk = mailbox_.pop<AttackMsg>(MsgType::Attack)) {
            auto wire = pack(MsgType::Attack, *atk);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }
        if (auto pick = mailbox_.pop<PickupMsg>(MsgType::Pickup)) {
            auto wire = pack(MsgType::Pickup, *pick);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }
        if (auto drop = mailbox_.pop<DropMsg>(MsgType::Drop)) {
            auto wire = pack(MsgType::Drop, *drop);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }
        if (auto swp = mailbox_.pop<InventorySwapMsg>(MsgType::InventorySwap)) {
            auto wire = pack(MsgType::InventorySwap, *swp);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }
        if (auto mv = mailbox_.pop<MoveGroundItemMsg>(MsgType::MoveGroundItem)) {
            auto wire = pack(MsgType::MoveGroundItem, *mv);
            sendWire(peer, 0, wire);
            enet_host_flush(client);
        }

        ENetEvent ev{};
        if (enet_host_service(client, &ev, 100) > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                try {
                    Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);
                    switch (env.type) {
                        case MsgType::Login:
                        case MsgType::Move:
                        case MsgType::Attack:
                        case MsgType::Pickup:
                        case MsgType::Drop:
                        case MsgType::InventorySwap:
                        case MsgType::MoveGroundItem:
                            // Client only expects server-originating state/response messages.
                            break;
                        case MsgType::Room: {
                            std::cout << "got a room!";
                            Room room = fromBytes<Room>(env.payload.data(), env.payload.size());
                            mailbox_.push(MsgType::Room, std::move(room));
                            break;
                        }
                        case MsgType::Chat: {
                            ChatMsg chat = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());
                            mailbox_.push(MsgType::Chat, std::move(chat));
                            break;
                        }
                        case MsgType::LoginResult: {
                            LoginResultMsg result = fromBytes<LoginResultMsg>(env.payload.data(), env.payload.size());
                            mailbox_.push(MsgType::LoginResult, std::move(result));
                            break;
                        }
                        case MsgType::GameState: {
                            GameStateMsg state = fromBytes<GameStateMsg>(env.payload.data(), env.payload.size());
                            mailbox_.push(MsgType::GameState, std::move(state));
                            break;
                        }
                        default:
                            break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[client] decode error: " << e.what() << "\n";
                }

                enet_packet_destroy(ev.packet);
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                die("disconnected early");
            }
        }

    }

    enet_peer_disconnect(peer, 0);
    enet_host_flush(client);
    enet_host_destroy(client);
}
