#include "net_client.h"

#include <enet/enet.h>

#include <cstdio>
#include <deque>

namespace netc {

namespace {

struct EnetInit {
    EnetInit()  { enet_initialize(); }
    ~EnetInit() { enet_deinitialize(); }
};
EnetInit g_enet_init;

constexpr int kHostChannels   = 1;
constexpr int kDisconnectWaitMs = 200;
constexpr int kDisconnectStepMs = 50;

void destroyPacket(ENetPacket* p) {
    if (p) enet_packet_destroy(p);
}

ENetPacket* makePacket(const std::vector<uint8_t>& bytes, bool reliable) {
    return enet_packet_create(bytes.data(), bytes.size(),
                              reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
}

} // namespace

struct NetClient::Impl {
    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;
    std::deque<IncomingMsg> inbox;

    ~Impl() {
        if (host) enet_host_destroy(host);
    }

    void send(const std::vector<uint8_t>& bytes, bool reliable) {
        if (!peer) return;
        enet_peer_send(peer, 0, makePacket(bytes, reliable));
    }

    void enqueueFromPacket(ENetPacket* pkt) {
        net::Reader r(pkt->data, pkt->dataLength);
        const auto type = static_cast<net::MsgType>(r.u8());
        switch (type) {
        case net::MsgType::Welcome: {
            uint32_t id, sx, sy, sz;
            if (net::readWelcome(r, id, sx, sy, sz)) {
                inbox.emplace_back(WelcomeEvent{id, sx, sy, sz});
            }
            break;
        }
        case net::MsgType::LoginResult: {
            net::LoginResultEvent e;
            if (net::readLoginResult(r, e)) inbox.emplace_back(std::move(e));
            break;
        }
        case net::MsgType::PlayerState: {
            net::PlayerSnapshot p;
            if (net::readPlayerState(r, p)) inbox.emplace_back(std::move(p));
            break;
        }
        case net::MsgType::PlayerLeave: {
            uint32_t id;
            if (net::readPlayerLeave(r, id)) inbox.emplace_back(net::PlayerLeaveEvent{id});
            break;
        }
        default: break;
        }
    }

    void waitForDisconnectAck() {
        ENetEvent ev;
        int budget = kDisconnectWaitMs;
        while (budget > 0 && enet_host_service(host, &ev, kDisconnectStepMs) > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE)    destroyPacket(ev.packet);
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) break;
            budget -= kDisconnectStepMs;
        }
    }
};

NetClient::NetClient() : impl_(std::make_unique<Impl>()) {
    impl_->host = enet_host_create(nullptr, 1, kHostChannels, 0, 0);
}

NetClient::~NetClient() { disconnect(); }

bool NetClient::connected() const { return impl_->peer != nullptr; }

bool NetClient::connect(const std::string& host_str, uint16_t port, int timeout_ms) {
    if (!impl_->host) return false;

    ENetAddress addr{};
    if (enet_address_set_host(&addr, host_str.c_str()) < 0) return false;
    addr.port = port;

    ENetPeer* peer = enet_host_connect(impl_->host, &addr, kHostChannels, 0);
    if (!peer) return false;

    ENetEvent ev;
    if (enet_host_service(impl_->host, &ev, timeout_ms) > 0
        && ev.type == ENET_EVENT_TYPE_CONNECT) {
        impl_->peer = peer;
        return true;
    }
    enet_peer_reset(peer);
    return false;
}

void NetClient::disconnect() {
    if (!impl_->peer) return;
    enet_peer_disconnect(impl_->peer, 0);
    impl_->waitForDisconnectAck();
    impl_->peer = nullptr;
}

void NetClient::pump(int timeout_ms) {
    ENetEvent ev;
    while (enet_host_service(impl_->host, &ev, timeout_ms) > 0) {
        timeout_ms = 0;
        switch (ev.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            impl_->enqueueFromPacket(ev.packet);
            destroyPacket(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            impl_->peer = nullptr;
            break;
        default: break;
        }
    }
}

std::optional<IncomingMsg> NetClient::pop() {
    if (impl_->inbox.empty()) return std::nullopt;
    IncomingMsg msg = std::move(impl_->inbox.front());
    impl_->inbox.pop_front();
    return msg;
}

void NetClient::sendLogin(const net::LoginPayload& payload) {
    impl_->send(net::writeLogin(payload), true);
}
void NetClient::sendMove(int8_t dx, int8_t dy) {
    impl_->send(net::writeMove(dx, dy), true);
}

} // namespace netc
