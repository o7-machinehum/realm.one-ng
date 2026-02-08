#pragma once

#include <enet/enet.h>
#include <thread>
#include <atomic>

#include "msg.h"
#include "envelope.h"

class World;
class AuthDb;

class NetServer {
public:
    NetServer(World& world, AuthDb& auth_db, uint16_t port)
        : world_(world), auth_db_(auth_db), port_(port)  {}

    void start() {
        recv_thread_ = std::thread(&NetServer::recvLoop, this);
    }

    void stop() {
        running_ = false;
        if (recv_thread_.joinable())
            recv_thread_.join();
    }

private:
    World& world_;
    AuthDb& auth_db_;
    std::thread recv_thread_;
    std::atomic<bool> running_{true};

    void recvLoop();
    uint16_t port_;
};
