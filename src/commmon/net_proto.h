#pragma once
#include <enet/enet.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>

#if defined(_WIN32)
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

namespace net {

enum class MsgType : uint16_t {
    Hello   = 1,
    Welcome = 2,
    Ping    = 3,
    Pong    = 4,
    Error   = 100,
};

static inline uint16_t ntoh16(uint16_t x) { return ntohs(x); }
static inline uint16_t hton16(uint16_t x) { return htons(x); }
static inline uint32_t ntoh32(uint32_t x) { return ntohl(x); }
static inline uint32_t hton32(uint32_t x) { return htonl(x); }

// ----- wire packet -----
struct PacketView {
    MsgType type{};
    std::vector<uint8_t> payload; // raw bytes
};

// Serialize: [u16 type][u32 len][payload]
static inline ENetPacket* make_packet(MsgType type,
                                      const void* payload,
                                      uint32_t payload_len,
                                      enet_uint32 flags = ENET_PACKET_FLAG_RELIABLE)
{
    const uint32_t header_size = 2 + 4;
    std::vector<uint8_t> buf;
    buf.resize(header_size + payload_len);

    uint16_t t = hton16((uint16_t)type);
    uint32_t n = hton32(payload_len);

    std::memcpy(buf.data() + 0, &t, 2);
    std::memcpy(buf.data() + 2, &n, 4);
    if (payload_len && payload) {
        std::memcpy(buf.data() + header_size, payload, payload_len);
    }

    return enet_packet_create(buf.data(), buf.size(), flags);
}

static inline ENetPacket* make_packet_str(MsgType type,
                                          const std::string& s,
                                          enet_uint32 flags = ENET_PACKET_FLAG_RELIABLE)
{
    return make_packet(type, s.data(), (uint32_t)s.size(), flags);
}

// Parse ENetPacket into PacketView. Returns nullopt if malformed.
static inline std::optional<PacketView> parse_packet(const ENetPacket* pkt)
{
    if (!pkt || pkt->dataLength < 6) return std::nullopt;

    uint16_t t_net;
    uint32_t n_net;
    std::memcpy(&t_net, pkt->data + 0, 2);
    std::memcpy(&n_net, pkt->data + 2, 4);

    uint16_t t = ntoh16(t_net);
    uint32_t n = ntoh32(n_net);

    if (pkt->dataLength != (size_t)(6 + n)) return std::nullopt;

    PacketView out;
    out.type = (MsgType)t;
    out.payload.resize(n);
    if (n) std::memcpy(out.payload.data(), pkt->data + 6, n);
    return out;
}

// Convenience to read payload as string
static inline std::string payload_as_string(const PacketView& pv) {
    return std::string((const char*)pv.payload.data(), (const char*)pv.payload.data() + pv.payload.size());
}

} // namespace net
