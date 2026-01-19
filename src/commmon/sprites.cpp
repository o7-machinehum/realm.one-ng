#include "sprites.h"
#include "tinyxml2.h"
#include "helpers.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>

using namespace tinyxml2;

static Dir parse_dir(const char* v) {
    if (!v || !v[0]) return Dir::S;
    char c = (char)std::toupper((unsigned char)v[0]);
    if (c == 'N') return Dir::N;
    if (c == 'S') return Dir::S;
    if (c == 'E') return Dir::E;
    if (c == 'W') return Dir::W;
    return Dir::S;
}

static const char* dir_name(Dir d) {
    switch (d) {
        case Dir::N: return "N";
        case Dir::E: return "E";
        case Dir::S: return "S";
        case Dir::W: return "W";
        default:     return "?";
    }
}

// -----------------------------
// internal access helpers
// -----------------------------

Clip& Sprites::clip(SpriteClips& sc, Dir d) {
    switch (d) {
        case Dir::N: return sc.n;
        case Dir::E: return sc.e;
        case Dir::S: return sc.s;
        case Dir::W: return sc.w;
    }
    return sc.s;
}

const Clip* Sprites::clip(const SpriteClips& sc, Dir d) {
    switch (d) {
        case Dir::N: return &sc.n;
        case Dir::E: return &sc.e;
        case Dir::S: return &sc.s;
        case Dir::W: return &sc.w;
    }
    return &sc.s;
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

    // iterate tiles
    for (XMLElement* tile = tileset->FirstChildElement("tile");
         tile;
         tile = tile->NextSiblingElement("tile"))
    {
        int id = -1;
        tile->QueryIntAttribute("id", &id);
        if (id < 0) continue;

        std::string sprite_name;
        Dir dir = Dir::S;
        int seq = -1;

        // defaults
        int frame_w = tile_w_;
        int frame_h = tile_h_;

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

            if (std::strcmp(pname, "name") == 0) {
                if (pval) sprite_name = pval;

            } else if (std::strcmp(pname, "dir") == 0) {
                dir = parse_dir(pval);

            } else if (std::strcmp(pname, "seq") == 0) {
                seq = atoi_safe(pval, -1);

            } else if (std::strcmp(pname, "size") == 0) {
                // nested class: <property name="size"><properties>...</properties></property>
                XMLElement* sub = p->FirstChildElement("properties");
                if (!sub) continue;

                for (XMLElement* sp = sub->FirstChildElement("property");
                     sp;
                     sp = sp->NextSiblingElement("property"))
                {
                    const char* sn = sp->Attribute("name");
                    const char* sv = sp->Attribute("value");
                    if (!sn || !sv) continue;

                    if (std::strcmp(sn, "x") == 0) frame_w = atoi_safe(sv, frame_w);
                    if (std::strcmp(sn, "y") == 0) frame_h = atoi_safe(sv, frame_h);
                }
            }
        }

        if (sprite_name.empty())
            continue;

        // compute base tile position
        int col = id % columns_;
        int row = id / columns_;

        int base_x = col * tile_w_;
        int base_y = row * tile_h_;

        // compute top-left of full frame (supports any height multiple of tile_h_)
        int tiles_high = frame_h / tile_h_;
        if (tiles_high < 1) tiles_high = 1;

        int top_row = row - (tiles_high - 1);
        if (top_row < 0) top_row = 0;

        int src_x = base_x;
        int src_y = top_row * tile_h_;

        Frame fr(id, seq, src_x, src_y, frame_w, frame_h);

        SpriteClips& sc = sprites_[sprite_name];
        clip(sc, dir).frames.push_back(fr);
    }

    // sort each clip
    for (auto& kv : sprites_) {
        sort_clip(kv.second.n);
        sort_clip(kv.second.e);
        sort_clip(kv.second.s);
        sort_clip(kv.second.w);
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

int Sprites::frame_count(const std::string& name, Dir dir) const
{
    auto it = sprites_.find(name);
    if (it == sprites_.end()) return 0;
    const Clip* c = clip(it->second, dir);
    return c ? (int)c->frames.size() : 0;
}

const Frame* Sprites::frame(const std::string& name, Dir dir, int index) const
{
    auto it = sprites_.find(name);
    if (it == sprites_.end()) return nullptr;
    const Clip* c = clip(it->second, dir);
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

