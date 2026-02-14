#include "sprites.h"
#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <unordered_map>

using namespace tinyxml2;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// -----------------------------
// internal access helpers
// -----------------------------

Clip& Sprites::clip(SpriteClips& sc, Dir d, ClipKind kind) {
    if (kind == ClipKind::Death) {
        return sc.d;
    } else if (kind == ClipKind::Action) {
        switch (d) {
            case Dir::N: return sc.an;
            case Dir::E: return sc.ae;
            case Dir::S: return sc.as;
            case Dir::W: return sc.aw;
        }
        return sc.as;
    } else {
        switch (d) {
            case Dir::N: return sc.n;
            case Dir::E: return sc.e;
            case Dir::S: return sc.s;
            case Dir::W: return sc.w;
        }
        return sc.s;
    }
}

const Clip* Sprites::clip(const SpriteClips& sc, Dir d, ClipKind kind) {
    if (kind == ClipKind::Death) {
        return &sc.d;
    } else if (kind == ClipKind::Action) {
        switch (d) {
            case Dir::N: return &sc.an;
            case Dir::E: return &sc.ae;
            case Dir::S: return &sc.as;
            case Dir::W: return &sc.aw;
        }
        return &sc.as;
    } else {
        switch (d) {
            case Dir::N: return &sc.n;
            case Dir::E: return &sc.e;
            case Dir::S: return &sc.s;
            case Dir::W: return &sc.w;
        }
        return &sc.s;
    }
}

// -----------------------------
// sorting
// -----------------------------

void Sprites::sort_clip(Clip& c) {
    std::sort(c.frames.begin(), c.frames.end(),
        [](const Frame& a, const Frame& b) {
            const int ka = (a.seq() >= 0) ? a.seq() : (1000000 + a.tile_id());
            const int kb = (b.seq() >= 0) ? b.seq() : (1000000 + b.tile_id());
            return (ka == kb) ? (a.tile_id() < b.tile_id()) : (ka < kb);
        });
}

// -----------------------------
// loader
// -----------------------------

bool Sprites::loadTSX(const std::string& tsx_path)
{
    static const SizeOverrideMap empty;
    return loadTSX(tsx_path, empty);
}

bool Sprites::loadTSX(const std::string& tsx_path, const SizeOverrideMap& size_overrides)
{
    sprites_.clear();
    tile_w_ = tile_h_ = columns_ = 0;
    image_source_.clear();

    XMLDocument doc;
    if (doc.LoadFile(tsx_path.c_str()) != XML_SUCCESS)
        return false;

    XMLElement* tileset = doc.FirstChildElement("tileset");
    if (!tileset) return false;

    tileset->QueryIntAttribute("tilewidth",  &tile_w_);
    tileset->QueryIntAttribute("tileheight", &tile_h_);
    tileset->QueryIntAttribute("columns",    &columns_);

    if (tile_w_ <= 0 || tile_h_ <= 0 || columns_ <= 0)
        return false;

    // texture source
    if (XMLElement* img = tileset->FirstChildElement("image")) {
        if (const char* src = img->Attribute("source"))
            image_source_ = src;
    }

    int tile_count = 0;
    tileset->QueryIntAttribute("tilecount", &tile_count);
    const int total_rows = std::max(1, (tile_count + columns_ - 1) / columns_);

    struct Anchor {
        int id = -1;
        int row = -1;
        int col = 0;
    };
    std::unordered_map<std::string, Anchor> anchors;

    // Collect sprite anchors from tile properties. An anchor tile is the top-left
    // of the full movement+action block.
    for (XMLElement* tile = tileset->FirstChildElement("tile");
         tile;
         tile = tile->NextSiblingElement("tile"))
    {
        int id = -1;
        tile->QueryIntAttribute("id", &id);
        if (id < 0) continue;

        std::string sprite_name;

        XMLElement* props = tile->FirstChildElement("properties");
        if (!props) continue;

        // read properties
        for (XMLElement* p = props->FirstChildElement("property");
             p;
             p = p->NextSiblingElement("property"))
        {
            const char* pname = p->Attribute("name");
            if (!pname) continue;

            const char* pval = p->Attribute("value");

            if ((std::strcmp(pname, "name") == 0 ||
                 std::strcmp(pname, "monster_name") == 0 ||
                 std::strcmp(pname, "npc_name") == 0) && pval) {
                sprite_name = pval;
            }
        }

        if (sprite_name.empty())
            continue;

        int col = id % columns_;
        int row = id / columns_;
        Anchor& a = anchors[sprite_name];
        if (a.id < 0 || row < a.row || (row == a.row && col < a.col)) {
            a.id = id;
            a.row = row;
            a.col = col;
        }
    }

    for (const auto& [name, anchor] : anchors) {
        int size_w_tiles = 1;
        int size_h_tiles = 2;
        auto it = size_overrides.find(name);
        if (it == size_overrides.end()) {
            it = size_overrides.find(toLower(name));
        }
        if (it != size_overrides.end()) {
            size_w_tiles = std::max(1, it->second.first);
            size_h_tiles = std::max(1, it->second.second);
        }

        const int frame_w = size_w_tiles * tile_w_;
        const int frame_h = size_h_tiles * tile_h_;
        const int action_w_tiles = size_w_tiles * 2;
        const int action_frame_w = action_w_tiles * tile_w_;
        constexpr int frames_per_dir = 4;
        const int block_top_row = anchor.row;
        const int block_left_col = anchor.col;

        // Direction rows from top to bottom: S, E, N, W.
        struct DirRow { Dir dir; int dir_idx; };
        static constexpr DirRow rows[] = {
            {Dir::S, 0}, {Dir::E, 1}, {Dir::N, 2}, {Dir::W, 3}
        };

        SpriteClips& sc = sprites_[name];
        for (const auto& dr : rows) {
            const int move_top_row = block_top_row + (dr.dir_idx * size_h_tiles);
            const int move_bottom_row = move_top_row + (size_h_tiles - 1);
            if (move_top_row < 0 || move_top_row >= total_rows) continue;
            if (move_top_row + size_h_tiles > total_rows) continue;

            for (int frame_idx = 0; frame_idx < frames_per_dir; ++frame_idx) {
                const int col_left = block_left_col + frame_idx * size_w_tiles;
                if (col_left < 0) continue;
                if (col_left + size_w_tiles > columns_) break;

                const int src_x = col_left * tile_w_;
                const int src_y = move_top_row * tile_h_;
                const int pseudo_tile_id = move_bottom_row * columns_ + col_left;
                clip(sc, dr.dir, ClipKind::Move).frames.emplace_back(pseudo_tile_id, frame_idx, src_x, src_y, frame_w, frame_h);
            }

            // Optional action strips are in the lower half, same row ordering,
            // but each frame is twice as wide on X.
            const int action_top_row = block_top_row + ((4 + dr.dir_idx) * size_h_tiles);
            const int action_bottom_row = action_top_row + (size_h_tiles - 1);
            if (action_top_row < 0 || action_top_row >= total_rows) continue;
            if (action_top_row + size_h_tiles > total_rows) continue;
            for (int frame_idx = 0; frame_idx < frames_per_dir; ++frame_idx) {
                const int action_col_left = block_left_col + frame_idx * action_w_tiles;
                if (action_col_left < 0) continue;
                if (action_col_left + action_w_tiles > columns_) break;

                const int src_x = action_col_left * tile_w_;
                const int src_y = action_top_row * tile_h_;
                const int pseudo_tile_id = action_bottom_row * columns_ + action_col_left;
                clip(sc, dr.dir, ClipKind::Action).frames.emplace_back(pseudo_tile_id, frame_idx, src_x, src_y, action_frame_w, frame_h);
            }
        }

        // Optional death block. Layout rule:
        // - starts one movement-frame width to the right of South frame #4
        // - starts (H-1) tiles lower than South frame #4 top
        // - dimensions are inverse: (H x W) tiles
        const int south_move_top_row = block_top_row;
        const int south_last_col_left = block_left_col + (frames_per_dir - 1) * size_w_tiles;
        const int death_col_left = south_last_col_left + size_w_tiles;
        const int death_top_row = south_move_top_row + (size_h_tiles - 1);
        const int death_w_tiles = size_h_tiles;
        const int death_h_tiles = size_w_tiles;
        if (death_col_left >= 0 &&
            death_col_left + death_w_tiles <= columns_ &&
            death_top_row >= 0 &&
            death_top_row + death_h_tiles <= total_rows) {
            const int src_x = death_col_left * tile_w_;
            const int src_y = death_top_row * tile_h_;
            const int death_bottom_row = death_top_row + death_h_tiles - 1;
            const int pseudo_tile_id = death_bottom_row * columns_ + death_col_left;
            clip(sc, Dir::S, ClipKind::Death)
                .frames.emplace_back(pseudo_tile_id, 0, src_x, src_y,
                                     death_w_tiles * tile_w_, death_h_tiles * tile_h_);
        }

        // If a sheet does not provide all 4 directional strips yet, reuse any
        // available strip so entities still render while art is in progress.
        const Clip* fallback = nullptr;
        if (!sc.s.frames.empty()) fallback = &sc.s;
        else if (!sc.e.frames.empty()) fallback = &sc.e;
        else if (!sc.n.frames.empty()) fallback = &sc.n;
        else if (!sc.w.frames.empty()) fallback = &sc.w;
        if (fallback) {
            if (sc.s.frames.empty()) sc.s.frames = fallback->frames;
            if (sc.e.frames.empty()) sc.e.frames = fallback->frames;
            if (sc.n.frames.empty()) sc.n.frames = fallback->frames;
            if (sc.w.frames.empty()) sc.w.frames = fallback->frames;
        }
    }

    // sort each clip
    for (auto& kv : sprites_) {
        sort_clip(kv.second.n);
        sort_clip(kv.second.e);
        sort_clip(kv.second.s);
        sort_clip(kv.second.w);
        sort_clip(kv.second.an);
        sort_clip(kv.second.ae);
        sort_clip(kv.second.as);
        sort_clip(kv.second.aw);
        sort_clip(kv.second.d);
    }

    return true;
}

// -----------------------------
// public accessors
// -----------------------------

const Sprite* Sprites::get(const std::string& name) const
{
    auto it = sprites_.find(name);
    if (it == sprites_.end()) return nullptr;

    static Sprite out;
    out.name = name;
    out.north.frames = it->second.n.frames;
    out.east.frames  = it->second.e.frames;
    out.south.frames = it->second.s.frames;
    out.west.frames  = it->second.w.frames;
    return &out;
}

int Sprites::frame_count(const std::string& name, Dir dir, ClipKind kind) const
{
    auto it = sprites_.find(name);
    if (it == sprites_.end()) return 0;
    const Clip* c = clip(it->second, dir, kind);
    return c ? (int)c->frames.size() : 0;
}

const Frame* Sprites::frame(const std::string& name, Dir dir, int index, ClipKind kind) const
{
    auto it = sprites_.find(name);
    if (it == sprites_.end()) return nullptr;
    const Clip* c = clip(it->second, dir, kind);
    if (!c) return nullptr;
    if (index < 0 || index >= (int)c->frames.size()) return nullptr;
    return &c->frames[index];
}

// -----------------------------
// debug dump
// -----------------------------

void Sprites::debug_dump() const
{
    printf("\n===== SPRITES DEBUG DUMP =====\n");
    printf("tile_w=%d tile_h=%d columns=%d\n", tile_w_, tile_h_, columns_);
    printf("image_source=%s\n\n", image_source_.c_str());

    for (const auto& kv : sprites_) {
        const std::string& name = kv.first;
        const SpriteClips& sc = kv.second;

        auto dump_clip = [&](const char* label, const Clip& c) {
            printf("Sprite '%s' Dir %s : %zu frames\n", name.c_str(), label, c.frames.size());

            for (size_t i = 0; i < c.frames.size(); ++i) {
                const Frame& f = c.frames[i];

                int col = f.tile_id() % columns_;
                int row = f.tile_id() / columns_;

                int base_x = col * tile_w_;
                int base_y = row * tile_h_;

                int tiles_high = f.rect().height / tile_h_;

                printf("  [%zu]\n", i);
                printf("    tile_id   = %d\n", f.tile_id());
                printf("    seq       = %d\n", f.seq());
                printf("    grid      = col=%d row=%d\n", col, row);
                printf("    base_src  = (%d, %d)\n", base_x, base_y);
                printf("    tiles_high= %d\n", tiles_high);
                printf("    final_src = (%d, %d)\n",
                       (int)f.rect().x,
                       (int)f.rect().y);
                printf("    size      = (%d x %d)\n",
                       (int)f.rect().width,
                       (int)f.rect().height);
            }
            printf("\n");
        };

        dump_clip("N", sc.n);
        dump_clip("E", sc.e);
        dump_clip("S", sc.s);
        dump_clip("W", sc.w);
    }

    printf("===== END DEBUG DUMP =====\n\n");
}
