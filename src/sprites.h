// sprites.h
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h" // Rectangle

enum class Dir : unsigned char { N, E, S, W };

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
    bool loadTSX(const std::string& tsx_path);

    // ---- access ----
    int frame_count(const std::string& name, Dir dir) const;
    const Frame* frame(const std::string& name, Dir dir, int index) const;

    const Sprite* get(const std::string& name) const;

    const std::string& image_source() const { return image_source_; }

    // ---- debug ----
    void debug_dump() const;

private:
    struct SpriteClips { Clip n, e, s, w; };

    static Clip& clip(SpriteClips& sc, Dir d);
    static const Clip* clip(const SpriteClips& sc, Dir d);
    static void sort_clip(Clip& c);

    int tile_w_ = 0;
    int tile_h_ = 0;
    int columns_ = 0;
    std::string image_source_;

    std::unordered_map<std::string, SpriteClips> sprites_;
};

