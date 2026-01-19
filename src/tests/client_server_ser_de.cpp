// enet_cereal_envelope_onefile.cpp
//
// Goal
// ----
// A *single* C++ file showing how to:
//   1) Serialize/deserialize C++ objects with cereal
//   2) Wrap them in an "Envelope" that includes a message type
//   3) Send/receive those bytes using ENet
//   4) Dispatch on the receiver by switching on the message type
//
// Why Envelope?
// ------------
// ENet sends raw bytes; cereal turns objects into bytes.
// The Envelope adds a small header (MsgType) so the receiver knows *which* object
// to deserialize out of the payload.
//
// Build (Linux)
// -------------
//   g++ -std=c++20 -O2 -Wall -Wextra enet_cereal_envelope_onefile.cpp -lenet -o demo
//
// cereal is header-only; install via your package manager, e.g.
//   Arch:   sudo pacman -S cereal
//   Debian: sudo apt install libcereal-dev
//
// Run
// ---
//   Terminal A: ./demo server 12345
//   Terminal B: ./demo client 127.0.0.1 12345
//
// Notes
// -----
// * This uses cereal's BinaryArchive. It's compact and fast, but not human-readable.
// * For a real protocol, you’ll likely add versioning and size limits.
// * You must include cereal type support headers for containers you use (string/vector here).

#include <enet/enet.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// cereal (binary archive + types we use)
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

// ============================================================================
// 1) Message "type tag" (what kind of message is this?)
// ============================================================================

enum class MsgType : uint16_t {
    Login = 1,
    Chat  = 2,
};

// ============================================================================
// 2) Envelope: {type, payload_bytes}
//
// The payload is cereal-serialized bytes of the actual message object.
// Receiver:
//   - Deserialize Envelope first
//   - switch(envelope.type)
//   - Deserialize payload into the correct message struct
// ============================================================================

struct Envelope {
    MsgType type{};
    std::vector<uint8_t> payload;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(type, payload);
    }
};

// ============================================================================
// 3) Example message structs
//    (Add as many as you want; each just needs serialize()).
// ============================================================================

struct LoginMsg {
    std::string user;
    std::string pass;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(user, pass);
    }
};

struct ChatMsg {
    std::string from;
    std::string text;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(from, text);
    }
};

// ============================================================================
// 4) cereal <-> bytes helpers
//
// toBytes(obj)     : serialize any cereal-serializable object into bytes
// fromBytes<T>(..) : deserialize bytes back into T
//
// These helpers keep the ENet code super small.
// ============================================================================

template <class T>
static std::vector<uint8_t> toBytes(const T& obj) {
    std::ostringstream oss(std::ios::binary);
    cereal::BinaryOutputArchive oar(oss);
    oar(obj);

    const std::string s = oss.str();
    return {s.begin(), s.end()};
}

template <class T>
static T fromBytes(const uint8_t* data, size_t len) {
    std::string s(reinterpret_cast<const char*>(data), len);
    std::istringstream iss(s, std::ios::binary);
    cereal::BinaryInputArchive iar(iss);

    T obj{};
    iar(obj);
    return obj;
}

// pack(type, msg) -> bytes for the whole Envelope (ready to send over ENet)
template <class T>
static std::vector<uint8_t> pack(MsgType type, const T& msg) {
    Envelope env{type, toBytes(msg)};
    return toBytes(env);
}

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
                            ChatMsg reply{"server", "welcome " + m.user};
                            auto wire = pack(MsgType::Chat, reply);
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

    // Send a Chat message
    {
        ChatMsg chat{"Ryan", "hello from client"};
        auto wire = pack(MsgType::Chat, chat);
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

                if (env.type == MsgType::Chat) {
                    ChatMsg m = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());
                    std::cout << "[client] CHAT from=" << m.from
                              << " text=" << m.text << "\n";
                    replies++;
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
