#pragma once
#include <enet/enet.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "fs_db.h"

struct ClientState {
    ENetPeer* peer = nullptr;
    bool authed = false;
    std::string name;
    std::string room;
    int x = 0;
    int y = 0;
};

class ServerNet {
public:
    ServerNet(int port, FsDb db);
    ~ServerNet();

    bool start();
    void tick(float dt);

private:
    void onConnect(ENetPeer* peer);
    void onDisconnect(ENetPeer* peer);
    void onReceive(ENetPeer* peer, const std::string& msg);

    void sendLine(ENetPeer* peer, const std::string& line);
    void sendRoom(ENetPeer* peer, const std::string& roomName);
    void sendStateToRoom(const std::string& roomName);

    ClientState* findClient(ENetPeer* peer);

    int port_ = 7777;
    FsDb db_;
    ENetHost* host_ = nullptr;

    std::unordered_map<ENetPeer*, ClientState> clients_;
    float stateAccum_ = 0.0f;
};
