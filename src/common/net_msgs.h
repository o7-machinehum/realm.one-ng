// Slim wire protocol for cube-world auth + walking + sync.
// All multi-byte ints are written little-endian.
// All packets start with a 1-byte MsgType tag.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace net {

enum class MsgType : uint8_t {
    Login       = 1,  // C->S: { username, public_key_hex, signature_hex, create_account }
    LoginResult = 2,  // S->C: { ok, message }
    Welcome     = 3,  // S->C: { your_player_id, world_size_x, world_size_y, world_size_z }
    Move        = 4,  // C->S: { dx, dy } in {-1,0,+1}
    PlayerState = 5,  // S->C: { id, x, y, z, facing, name }
    PlayerLeave = 6,  // S->C: { id }
};

enum class Facing : uint8_t { South = 0, East = 1, North = 2, West = 3 };

struct LoginPayload {
    std::string username;
    std::string public_key_hex;
    std::string signature_hex;
    bool        create_account;
};

struct LoginResultEvent {
    bool        ok;
    std::string message;
};

struct PlayerSnapshot {
    uint32_t id;
    int32_t  x;
    int32_t  y;
    int32_t  z;
    uint8_t  facing;
    std::string name;
};

struct PlayerLeaveEvent {
    uint32_t id;
};

// ----- tiny binary writer/reader over a byte vector -----------------------
class Writer {
public:
    std::vector<uint8_t> buf;
    void u8 (uint8_t v)  { buf.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void i32(int32_t v)  { u32(static_cast<uint32_t>(v)); }
    void str(const std::string& s) {
        const uint32_t n = static_cast<uint32_t>(s.size());
        u32(n);
        buf.insert(buf.end(), s.begin(), s.end());
    }
};

class Reader {
public:
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    Reader(const uint8_t* data, size_t len) : p(data), end(data + len) {}
    uint8_t u8() {
        if (p >= end) { ok = false; return 0; }
        return *p++;
    }
    uint32_t u32() {
        if (p + 4 > end) { ok = false; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
        p += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    std::string str() {
        const uint32_t n = u32();
        if (!ok || p + n > end) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    }
};

// ----- Login ---------------------------------------------------------------
inline std::vector<uint8_t> writeLogin(const LoginPayload& l) {
    Writer w;
    w.u8(static_cast<uint8_t>(MsgType::Login));
    w.str(l.username);
    w.str(l.public_key_hex);
    w.str(l.signature_hex);
    w.u8(l.create_account ? 1 : 0);
    return std::move(w.buf);
}
inline bool readLogin(Reader& r, LoginPayload& out) {
    out.username       = r.str();
    out.public_key_hex = r.str();
    out.signature_hex  = r.str();
    out.create_account = (r.u8() != 0);
    return r.ok;
}

// ----- LoginResult ---------------------------------------------------------
inline std::vector<uint8_t> writeLoginResult(bool ok, const std::string& msg) {
    Writer w;
    w.u8(static_cast<uint8_t>(MsgType::LoginResult));
    w.u8(ok ? 1 : 0);
    w.str(msg);
    return std::move(w.buf);
}
inline bool readLoginResult(Reader& r, LoginResultEvent& out) {
    out.ok      = (r.u8() != 0);
    out.message = r.str();
    return r.ok;
}

// ----- Welcome -------------------------------------------------------------
inline std::vector<uint8_t> writeWelcome(uint32_t your_id, uint32_t sx, uint32_t sy, uint32_t sz) {
    Writer w; w.u8(static_cast<uint8_t>(MsgType::Welcome));
    w.u32(your_id); w.u32(sx); w.u32(sy); w.u32(sz);
    return std::move(w.buf);
}
inline bool readWelcome(Reader& r, uint32_t& your_id, uint32_t& sx, uint32_t& sy, uint32_t& sz) {
    your_id = r.u32(); sx = r.u32(); sy = r.u32(); sz = r.u32(); return r.ok;
}

// ----- Move ----------------------------------------------------------------
inline std::vector<uint8_t> writeMove(int8_t dx, int8_t dy) {
    Writer w; w.u8(static_cast<uint8_t>(MsgType::Move));
    w.u8(static_cast<uint8_t>(dx));
    w.u8(static_cast<uint8_t>(dy));
    return std::move(w.buf);
}
inline bool readMove(Reader& r, int8_t& dx, int8_t& dy) {
    dx = static_cast<int8_t>(r.u8());
    dy = static_cast<int8_t>(r.u8());
    return r.ok;
}

// ----- PlayerState ---------------------------------------------------------
inline std::vector<uint8_t> writePlayerState(const PlayerSnapshot& p) {
    Writer w; w.u8(static_cast<uint8_t>(MsgType::PlayerState));
    w.u32(p.id); w.i32(p.x); w.i32(p.y); w.i32(p.z); w.u8(p.facing); w.str(p.name);
    return std::move(w.buf);
}
inline bool readPlayerState(Reader& r, PlayerSnapshot& p) {
    p.id = r.u32(); p.x = r.i32(); p.y = r.i32(); p.z = r.i32();
    p.facing = r.u8(); p.name = r.str(); return r.ok;
}

// ----- PlayerLeave ---------------------------------------------------------
inline std::vector<uint8_t> writePlayerLeave(uint32_t id) {
    Writer w; w.u8(static_cast<uint8_t>(MsgType::PlayerLeave)); w.u32(id);
    return std::move(w.buf);
}
inline bool readPlayerLeave(Reader& r, uint32_t& id) { id = r.u32(); return r.ok; }

} // namespace net
