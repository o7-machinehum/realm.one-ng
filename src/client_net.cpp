#include "client_net.h"
#include <sstream>
#include <iostream>

static std::string trim(std::string s) {
    auto ws = [](unsigned char c){ return c <= 32; };
    while (!s.empty() && ws((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && ws((unsigned char)s.back())) s.pop_back();
    return s;
}

ClientNet::ClientNet() {
    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
}
ClientNet::~ClientNet() {
    disconnect();
    if (host_) enet_host_destroy(host_);
}

bool ClientNet::connectTo(const std::string& ip, int port) {
    if (!host_) return false;
    if (peer_) return true;

    ENetAddress addr{};
    enet_address_set_host(&addr, ip.c_str());
    addr.port = (enet_uint16)port;

    peer_ = enet_host_connect(host_, &addr, 2, 0);
    if (!peer_) return false;

    status_ = "Connecting...";
    return true;
}

void ClientNet::disconnect() {
    if (peer_) {
        enet_peer_disconnect(peer_, 0);
        peer_ = nullptr;
    }
    authed_ = false;
    myName_.clear();
    room_.clear();
    others_.clear();
    expectingRoomBlob_ = false;
    roomTmxReady_.reset();
}

void ClientNet::sendLine(const std::string& line) {
    if (!peer_) {
        std::cout << "Peer == 0" << std::endl;
        return;
    }

    std::string s = line;
    if (s.empty() || s.back() != '\n') s.push_back('\n');
    ENetPacket* pkt = enet_packet_create(s.data(), s.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer_, 0, pkt);
}

std::optional<std::string> ClientNet::takeRoomTmx() {
    auto out = roomTmxReady_;
    roomTmxReady_.reset();
    return out;
}

std::optional<std::string> ClientNet::takeStatusLine() {
    auto out = status_;
    status_.reset();
    return out;
}

void ClientNet::handleLine(const std::string& lineIn) {
    std::string line = trim(lineIn);
    if (line.empty()) return;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "NEED_CREATE") { status_ = "Server: /create <name> <pass>"; return; }
    if (cmd == "NEED_IDENTIFY") { status_ = "Server: /identify <name> <pass>"; return; }

    if (cmd == "CREATE_OK") { iss >> myName_; authed_ = true; status_ = "Created: " + myName_; return; }
    if (cmd == "IDENTIFY_OK") { iss >> myName_; authed_ = true; status_ = "Logged in: " + myName_; return; }

    if (cmd == "ERROR") { status_ = "Server ERROR: " + line.substr(6); return; }

    if (cmd == "ROOM_BEGIN") {
        iss >> roomNamePending_ >> roomBytesExpected_;
        expectingRoomBlob_ = true;
        roomBlob_.clear();
        return;
    }
    if (cmd == "ROOM_END") {
        if (!expectingRoomBlob_) return;
        expectingRoomBlob_ = false;
        room_ = roomNamePending_;
        roomTmxReady_ = roomBlob_;
        status_ = "Room received: " + room_;
        return;
    }

    if (cmd == "STATE") {
        std::string room;
        int meX, meY, count;
        iss >> room >> meX >> meY >> count;
        room_ = room;
        myX_ = meX;
        myY_ = meY;
        others_.clear();
        others_.reserve((size_t)count);
        // remaining lines arrive in same packet; we parse in tick() (multi-line)
        return;
    }
}

void ClientNet::tick() {
    if (!host_) return;

    ENetEvent ev;
    while (enet_host_service(host_, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                status_ = "Connected. Waiting for auth instructions...";
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                status_ = "Disconnected.";
                disconnect();
                break;

            case ENET_EVENT_TYPE_RECEIVE: {
                // If expecting room blob, this packet might be raw XML (no newlines needed)
                if (expectingRoomBlob_ && ev.packet->dataLength == roomBytesExpected_) {
                    roomBlob_.assign((char*)ev.packet->data, (char*)ev.packet->data + ev.packet->dataLength);
                    enet_packet_destroy(ev.packet);
                    break;
                }

                std::string msg((char*)ev.packet->data, (char*)ev.packet->data + ev.packet->dataLength);
                enet_packet_destroy(ev.packet);

                // Parse multi-line packets
                std::istringstream ss(msg);
                std::string line;
                while (std::getline(ss, line)) {
                    line = trim(line);
                    if (line.empty()) continue;

                    if (line.rfind("P ", 0) == 0) {
                        std::istringstream ps(line);
                        std::string P, name; int x, y;
                        ps >> P >> name >> x >> y;
                        others_.push_back(RemotePlayer{name, x, y});
                        continue;
                    }

                    if (line == "END") continue;

                    handleLine(line);
                }
                break;
            }

            default:
                break;
        }
    }
}
