#include "combat_fx.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include <tinyxml2.h>

namespace client {

std::vector<int> buildFrameSequence(std::vector<std::pair<int, int>> seq_pairs,
                                     int fallback_anchor) {
    if (seq_pairs.empty()) return {std::max(0, fallback_anchor)};
    std::sort(seq_pairs.begin(), seq_pairs.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    std::vector<int> out;
    out.reserve(seq_pairs.size());
    for (const auto& [_, tile_id] : seq_pairs) out.push_back(tile_id);
    return out;
}

const CombatFxAtlas& combatFxAtlas() {
    static CombatFxAtlas atlas{};
    if (atlas.loaded) return atlas;
    atlas.loaded = true;

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile("game/assets/art/properties.tsx") != tinyxml2::XML_SUCCESS) {
        atlas.hit_frames   = {0, 1, 2};
        atlas.wiff_frames  = {40, 41, 42};
        atlas.block_frames = {80, 81, 82};
        return atlas;
    }

    const tinyxml2::XMLElement* root = doc.FirstChildElement("tileset");
    if (!root) return atlas;
    if (const char* c = root->Attribute("columns"))    atlas.columns = std::max(1, std::atoi(c));
    if (const char* tw = root->Attribute("tilewidth"))  atlas.tile_w  = std::max(1, std::atoi(tw));
    if (const char* th = root->Attribute("tileheight")) atlas.tile_h  = std::max(1, std::atoi(th));

    std::vector<std::pair<int, int>> hit_seq_pairs;
    std::vector<std::pair<int, int>> wiff_seq_pairs;
    std::vector<std::pair<int, int>> block_seq_pairs;
    std::string last_desc;

    for (const tinyxml2::XMLElement* tile = root->FirstChildElement("tile");
         tile != nullptr;
         tile = tile->NextSiblingElement("tile")) {
        int tile_id = -1;
        tile->QueryIntAttribute("id", &tile_id);
        if (tile_id < 0) continue;

        std::string desc;
        int seq_idx = -1;
        const tinyxml2::XMLElement* props = tile->FirstChildElement("properties");
        if (props) {
            for (const tinyxml2::XMLElement* p = props->FirstChildElement("property");
                 p != nullptr;
                 p = p->NextSiblingElement("property")) {
                const char* name = p->Attribute("name");
                const char* value = p->Attribute("value");
                if (!name || !value) continue;
                std::string n = name;
                std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (n == "description") desc = value;
                else if (n == "seq" || n == "sequence") {
                    try { seq_idx = std::stoi(value); } catch (...) {}
                }
            }
        }
        if (!desc.empty()) {
            std::transform(desc.begin(), desc.end(), desc.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            last_desc = desc;
        } else if (seq_idx >= 0 && !last_desc.empty()) {
            desc = last_desc;
        }
        if (desc.empty()) continue;
        if (seq_idx < 0) seq_idx = tile_id;
        if (desc == "hit") hit_seq_pairs.push_back({seq_idx, tile_id});
        else if (desc == "wiff" || desc == "whiff" || desc == "miss") wiff_seq_pairs.push_back({seq_idx, tile_id});
        else if (desc == "block" || desc == "blocked") block_seq_pairs.push_back({seq_idx, tile_id});
    }

    atlas.hit_frames   = buildFrameSequence(std::move(hit_seq_pairs), 0);
    atlas.wiff_frames  = buildFrameSequence(std::move(wiff_seq_pairs), 40);
    atlas.block_frames = buildFrameSequence(std::move(block_seq_pairs), 80);
    return atlas;
}

} // namespace client
