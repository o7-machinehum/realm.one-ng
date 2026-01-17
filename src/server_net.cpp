#include "server_net.h"
#include <iostream>
#include <sstream>

static std::string trim(std::string s) {
    auto ws = [](unsigned char c){ return c <= 32; };
    while (!s.empty() && ws((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && ws((unsigned char)s.back())) s.pop_back();
    return s;
}

ServerNet::ServerNet(int port, FsDb db) : port_(port), db_(std::move(db)) {}
ServerNet::~ServerNet() {
    if (host_) enet_host_destroy(host_);
}

bool ServerNet::start() {
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16)port_;

    host_ = enet_host_create(&addr, 64, 2, 0, 0);
    if (!host_) {
        std::cerr << "enet_host_create failed\n";
        return false;
    }
    std::cout << "Server listening on UDP/" << port_ << "\n";
    return true;
}

ClientState* ServerNet::findClient(ENetPeer* peer) {
    auto it = clients_.find(peer);
    if (it == clients_.end()) return nullptr;
    return &it->second;
}

void ServerNet::sendLine(ENetPeer* peer, const std::string& line) {
    std::cout << "Sending: " << line << "\n";
    std::string s = line;
    if (s.empty() || s.back() != '\n') s.push_back('\n');
    ENetPacket* pkt = enet_packet_create(s.data(), s.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, pkt);
}

void ServerNet::sendRoom(ENetPeer* peer, const std::string& roomName) {
    auto tmxOpt = db_.loadRoomTmx(roomName);
    if (!tmxOpt) {
        sendLine(peer, "ERROR ROOM_NOT_FOUND " + roomName);
        return;
    }
    const std::string& tmx = *tmxOpt;

    // Header line
    sendLine(peer, "ROOM_BEGIN " + roomName + " " + std::to_string(tmx.size()));

    // Raw blob packet (binary-safe)
    ENetPacket* blob = enet_packet_create(tmx.data(), tmx.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, blob);

    // Footer line
    sendLine(peer, "ROOM_END");
}

void ServerNet::onConnect(ENetPeer* peer) {
    char ip[64] = {0};
    enet_address_get_host_ip(&peer->address, ip, sizeof(ip));
    std::cout << "Client connected: " << ip << ":" << peer->address.port << "\n";

    // Explicit: tell them what to do
    sendLine(peer, "NEED_CREATE");
    sendLine(peer, "NEED_IDENTIFY");
}

void ServerNet::onDisconnect(ENetPeer* peer) {
    auto it = clients_.find(peer);
    if (it != clients_.end()) {
        if (it->second.authed) {
            CharacterRecord rec;
            rec.name = it->second.name;
            rec.password = db_.loadCharacter(rec.name)->password; // keep existing
            rec.room = it->second.room;
            rec.x = it->second.x;
            rec.y = it->second.y;
            db_.saveCharacter(rec);
        }
        clients_.erase(it);
    }
}

void ServerNet::sendStateToRoom(const std::string& roomName) {
    // Gather players in room
    std::vector<ClientState*> players;
    for (auto& kv : clients_) {
        if (kv.second.authed && kv.second.room == roomName) players.push_back(&kv.second);
    }
    if (players.empty()) return;

    // For each player, send state (includes other players)
    for (auto* me : players) {
        std::ostringstream ss;
        ss << "STATE " << me->room << " " << me->x << " " << me->y << " " << (players.size() - 1) << "\n";
        for (auto* p : players) {
            if (p == me) continue;
            ss << "P " << p->name << " " << p->x << " " << p->y << "\n";
        }
        ss << "END\n";

        auto out = ss.str();
        ENetPacket* pkt = enet_packet_create(out.data(), out.size(), 0);
        enet_peer_send(me->peer, 0, pkt);
    }
}

void ServerNet::onReceive(ENetPeer* peer, const std::string& msgIn) {
    std::cout << "Recived: " << msgIn << std::endl;
    auto* c = findClient(peer);
    if (!c) return;


    std::string msg = trim(msgIn);
    if (msg.empty()) return;

    std::istringstream iss(msg);
    std::string cmd;
    iss >> cmd;

    if (cmd == "CREATE") {
        std::string name, pass;
        iss >> name >> pass;
        if (name.empty() || pass.empty()) { sendLine(peer, "ERROR BAD_ARGS CREATE"); return; }
        if (db_.characterExists(name)) { sendLine(peer, "ERROR NAME_TAKEN"); return; }

        CharacterRecord rec;
        rec.name = name;
        rec.password = pass;
        rec.room = "d1";
        rec.x = 5;
        rec.y = 5;
        db_.saveCharacter(rec);

        c->authed = true;
        c->name = name;
        c->room = rec.room;
        c->x = rec.x;
        c->y = rec.y;

        sendLine(peer, "CREATE_OK " + name);
        sendRoom(peer, c->room);
        sendStateToRoom(c->room);
        return;
    }

    if (cmd == "IDENTIFY") {
        std::string name, pass;
        iss >> name >> pass;
        if (name.empty() || pass.empty()) { sendLine(peer, "ERROR BAD_ARGS IDENTIFY"); return; }

        auto recOpt = db_.loadCharacter(name);
        if (!recOpt) { sendLine(peer, "ERROR NO_SUCH_USER"); return; }
        if (recOpt->password != pass) { sendLine(peer, "ERROR BAD_PASSWORD"); return; }

        c->authed = true;
        c->name = recOpt->name;
        c->room = recOpt->room;
        c->x = recOpt->x;
        c->y = recOpt->y;

        sendLine(peer, "IDENTIFY_OK " + name);
        sendRoom(peer, c->room);
        sendStateToRoom(c->room);
        return;
    }

    if (cmd == "LOGOUT") {
        if (c->authed) {
            CharacterRecord rec;
            rec.name = c->name;
            rec.password = db_.loadCharacter(rec.name)->password;
            rec.room = c->room;
            rec.x = c->x;
            rec.y = c->y;
            db_.saveCharacter(rec);
        }
        sendLine(peer, "OK LOGOUT");
        enet_peer_disconnect(peer, 0);
        return;
    }

    if (cmd == "MOVE") {
        if (!c->authed) { sendLine(peer, "ERROR NOT_AUTHED"); return; }
        char d = 0;
        iss >> d;
        if (!d) { sendLine(peer, "ERROR BAD_ARGS MOVE"); return; }

        int dx = 0, dy = 0;
        if (d == 'L') dx = -1;
        else if (d == 'R') dx = 1;
        else if (d == 'U') dy = -1;
        else if (d == 'D') dy = 1;
        else { sendLine(peer, "ERROR BAD_DIR"); return; }

        c->x += dx;
        c->y += dy;

        // For now, save every move (fine for prototype)
        CharacterRecord rec;
        rec.name = c->name;
        rec.password = db_.loadCharacter(rec.name)->password;
        rec.room = c->room;
        rec.x = c->x;
        rec.y = c->y;
        db_.saveCharacter(rec);

        return;
    }

    sendLine(peer, "ERROR UNKNOWN_CMD " + cmd);
}

void ServerNet::tick(float dt) {
    if (!host_) return;

    // Pump events
    ENetEvent ev;
    while (enet_host_service(host_, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_RECEIVE:
                onReceive(ev.peer, std::string((char*)ev.packet->data, ev.packet->dataLength));
                enet_packet_destroy(ev.packet);
                break;
            case ENET_EVENT_TYPE_CONNECT:
                clients_[ev.peer] = ClientState{ev.peer};
                onConnect(ev.peer);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                onDisconnect(ev.peer);
                break;
            default: break;
        }
    }

    // Periodic state broadcast
    stateAccum_ += dt;
    if (stateAccum_ >= 0.1f) { // 10 Hz
        stateAccum_ = 0.0f;
        // broadcast per room that has players
        std::unordered_map<std::string, bool> rooms;
        for (auto& kv : clients_) {
            if (kv.second.authed) rooms[kv.second.room] = true;
        }
        for (auto& kv : rooms) sendStateToRoom(kv.first);
        enet_host_flush(host_);
    }
}
