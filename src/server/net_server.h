#pragma once

#include <enet/enet.h>
#include <thread>
#include <atomic>

#include "msg.h"
#include "envelope.h"

class NetServer {
public:
    NetServer(Mailbox& mailbox, uint16_t port)
        : mailbox_(mailbox), port_(port)  {}

    void start() {
        recv_thread_ = std::thread(&NetServer::recvLoop, this);
    }

    void stop() {
        running_ = false;
        if (recv_thread_.joinable())
            recv_thread_.join();
    }

private:
    Mailbox& mailbox_;
    std::thread recv_thread_;
    std::atomic<bool> running_{true};

    void recvLoop();
    uint16_t port_;
};
