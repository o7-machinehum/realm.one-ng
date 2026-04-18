// sprites.h
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "combat_types.h"
#include "raylib.h" // Rectangle

// Cardinal direction for sprite sheet row selection (rendering order).
// See also Facing in combat_types.h, which is for game logic / network protocol.
enum class Dir : unsigned char { N, E, S, W };

// Converts a Facing (game logic) to a Dir (sprite rendering).
[[nodiscard]] constexpr Dir facingToDir(Facing f) {
    switch (f) {
        case Facing::North: return Dir::N;
        case Facing::East:  return Dir::E;
        case Facing::South: return Dir::S;
        case Facing::West:  return Dir::W;
    }
    return Dir::S;
}

// Converts a Dir (sprite rendering) to a Facing (game logic).
[[nodiscard]] constexpr Facing dirToFacing(Dir d) {
    switch (d) {
        case Dir::N: return Facing::North;
        case Dir::E: return Facing::East;
        case Dir::S: return Facing::South;
        case Dir::W: return Facing::West;
    }
    return Facing::South;
}
enum class ClipKind : unsigned char { Move, Action, Death };

class Frame {
public:
    Frame() = default;
    Frame(int tile_id, int seq, int src_x, int src_y, int w, int h)
        : tile_id_(tile_id), seq_(seq), src_x_(src_x), src_y_(src_y), w_(w), h_(h) {}

    int tile_id() const { return tile_id_; }
    int seq() const { return seq_; }

    Rectangle rect() const {
        return Rectangle{ (float)src_x_, (float)src_y_, (float)w_, (float)h_ };
    }

private:
    int tile_id_ = -1;
    int seq_ = -1;
    int src_x_ = 0;
    int src_y_ = 0;
    int w_ = 0;
    int h_ = 0;
};

struct Clip { std::vector<Frame> frames; };

struct Sprite {
    std::string name;
    Clip north, south, east, west;
};


class Sprites {
public:
    using SizeOverrideMap = std::unordered_map<std::string, std::pair<int, int>>;

    bool loadTSX(const std::string& tsx_path);
    bool loadTSX(const std::string& tsx_path, const SizeOverrideMap& size_overrides);

    // ---- access ----
    int frame_count(const std::string& name, Dir dir, ClipKind kind = ClipKind::Move) const;
    const Frame* frame(const std::string& name, Dir dir, int index, ClipKind kind = ClipKind::Move) const;

    const Sprite* get(const std::string& name) const;

    const std::string& image_source() const { return image_source_; }

    // ---- debug ----
    void debug_dump() const;

private:
    struct SpriteClips { Clip n, e, s, w; Clip an, ae, as, aw; Clip d; };

    static Clip& clip(SpriteClips& sc, Dir d, ClipKind kind);
    static const Clip* clip(const SpriteClips& sc, Dir d, ClipKind kind);
    static void sort_clip(Clip& c);

    int tile_w_ = 0;
    int tile_h_ = 0;
    int columns_ = 0;
    std::string image_source_;

    std::unordered_map<std::string, SpriteClips> sprites_;
};
