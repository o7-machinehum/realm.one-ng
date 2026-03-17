#include "room.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace tinyxml2;

// ---------- small helpers ----------
static std::string readFileToString(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string trimStr(std::string s) {
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && is_space((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_space((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string normalizedLayerName(const std::string& in) {
    std::string out = trimStr(in);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

static bool isCollisionMetadataLayerName(const std::string& name) {
    const std::string n = normalizedLayerName(name);
    return n == "monsters" || n == "items" || n == "npcs";
}

static std::vector<uint32_t> parseCsvU32(const char* text) {
    std::vector<uint32_t> out;
    if (!text) return out;

    std::string s = text;
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }

    std::stringstream ss(s);
    while (ss.good()) {
        std::string token;
        if (!std::getline(ss, token, ',')) break;
        token = trimStr(token);
        if (token.empty()) continue;

        try {
            // TMX gids can include flip flags (high bits). Keep full 32-bit.
            uint32_t v = (uint32_t)std::stoul(token);
            out.push_back(v);
        } catch (...) {
            // soft fail; you can make strict later
        }
    }
    return out;
}

static std::map<int, bool> loadWalkableTileProps(const std::filesystem::path& tsx_path) {
    std::map<int, bool> out;
    XMLDocument doc;
    if (doc.LoadFile(tsx_path.string().c_str()) != XML_SUCCESS) return out;

    XMLElement* tileset = doc.FirstChildElement("tileset");
    if (!tileset) return out;

    for (XMLElement* tile = tileset->FirstChildElement("tile");
         tile;
         tile = tile->NextSiblingElement("tile")) {
        int id = -1;
        tile->QueryIntAttribute("id", &id);
        if (id < 0) continue;

        XMLElement* props = tile->FirstChildElement("properties");
        if (!props) continue;
        for (XMLElement* p = props->FirstChildElement("property");
             p;
             p = p->NextSiblingElement("property")) {
            const char* pname = p->Attribute("name");
            if (!pname) continue;

            auto parse_bool_text = [](const char* raw, bool& v) -> bool {
                if (!raw) return false;
                std::string s = trimStr(raw);
                if (s.empty()) return false;
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
                if (s == "1" || s == "true" || s == "yes" || s == "on") {
                    v = true;
                    return true;
                }
                if (s == "0" || s == "false" || s == "no" || s == "off") {
                    v = false;
                    return true;
                }
                return false;
            };

            auto read_bool = [&](bool& v) -> bool {
                const char* value_attr = p->Attribute("value");
                if (parse_bool_text(value_attr, v)) return true;
                if (p->QueryBoolAttribute("value", &v) == XML_SUCCESS) return true;
                const char* txt = p->GetText();
                if (!parse_bool_text(txt, v)) return false;
                return true;
            };

            bool b = false;
            if (std::strcmp(pname, "non_walkable") == 0) {
                if (read_bool(b)) out[id] = !b;
            } else {
                continue;
            }
        }
    }
    return out;
}

static std::map<int, std::string> loadTileStringProps(const std::filesystem::path& tsx_path, const char* key_name) {
    std::map<int, std::string> out;
    XMLDocument doc;
    if (doc.LoadFile(tsx_path.string().c_str()) != XML_SUCCESS) return out;

    XMLElement* tileset = doc.FirstChildElement("tileset");
    if (!tileset) return out;

    for (XMLElement* tile = tileset->FirstChildElement("tile");
         tile;
         tile = tile->NextSiblingElement("tile")) {
        int id = -1;
        tile->QueryIntAttribute("id", &id);
        if (id < 0) continue;

        XMLElement* props = tile->FirstChildElement("properties");
        if (!props) continue;
        for (XMLElement* p = props->FirstChildElement("property");
             p;
             p = p->NextSiblingElement("property")) {
            const char* pname = p->Attribute("name");
            if (!pname || std::strcmp(pname, key_name) != 0) continue;

            const char* v = p->Attribute("value");
            if (!v) v = p->GetText();
            if (!v || !*v) continue;
            out[id] = v;
            break;
        }
    }
    return out;
}

// ---------- Room impl ----------
bool Room::loadFromFile(const std::filesystem::path& tmx_path) {
    name_ = tmx_path.filename().string();
    const std::string xml = readFileToString(tmx_path);
    if (xml.empty()) {
        std::fprintf(stderr, "TMX read failed: %s\n", tmx_path.string().c_str());
        return false;
    }

    XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) {
        std::fprintf(stderr, "TMX parse failed: %s\n", tmx_path.string().c_str());
        return false;
    }

    return parseTmxDoc(doc);
}


bool Room::parseTmxDoc(XMLDocument& doc) {
    tilesets_.clear();
    layers_.clear();
    portals_.clear();
    monster_spawns_.clear();
    item_spawns_.clear();
    npc_spawns_.clear();
    walkable_mask_.clear();
    props_.clear();

    XMLElement* map = doc.FirstChildElement("map");
    if (!map) return false;

    // required-ish map attrs (QueryIntAttribute leaves value unchanged on failure)
    map_w_  = 0; map_h_ = 0;
    tile_w_ = 0; tile_h_ = 0;
    map->QueryIntAttribute("width", &map_w_);
    map->QueryIntAttribute("height", &map_h_);
    map->QueryIntAttribute("tilewidth", &tile_w_);
    map->QueryIntAttribute("tileheight", &tile_h_);

    if (map_w_ <= 0 || map_h_ <= 0 || tile_w_ <= 0 || tile_h_ <= 0) return false;

    // ---- properties -> flat dict (dot keys, supports type="class") ----
    struct Frame { XMLElement* props; std::string prefix; };
    std::vector<Frame> stack;

    if (XMLElement* props = map->FirstChildElement("properties")) {
        stack.push_back({ props, "" });
    }

    while (!stack.empty()) {
        Frame fr = std::move(stack.back());
        stack.pop_back();

        for (XMLElement* p = fr.props->FirstChildElement("property");
             p;
             p = p->NextSiblingElement("property")) {

            const char* name = p->Attribute("name");
            if (!name) continue;

            std::string key = fr.prefix.empty() ? std::string(name)
                                                : (fr.prefix + "." + name);

            const char* type = p->Attribute("type");
            if (type && std::strcmp(type, "class") == 0) {
                if (XMLElement* childProps = p->FirstChildElement("properties")) {
                    stack.push_back({ childProps, std::move(key) });
                }
                continue;
            }

            const char* v = p->Attribute("value");
            if (!v) v = p->GetText();
            props_[key] = v ? v : "";
        }
    }

    // ---- tilesets (external TSX only, as your renderer expects) ----
    std::vector<std::map<int, bool>> ts_walk_props;
    std::vector<std::map<int, std::string>> ts_monster_props;
    std::vector<std::map<int, std::string>> ts_item_props;
    std::vector<std::map<int, std::string>> ts_npc_props;
    for (XMLElement* ts = map->FirstChildElement("tileset");
         ts;
         ts = ts->NextSiblingElement("tileset")) {

        RoomTilesetRef r;
        r.first_gid = 0;
        ts->QueryIntAttribute("firstgid", &r.first_gid);

        const char* src = ts->Attribute("source");
        if (!src || r.first_gid <= 0) return false;

        // Extract path relative to game/assets/art/ (e.g. "monsters/slime.tsx")
        {
            const std::string src_str(src);
            const std::string marker = "game/assets/art/";
            auto pos = src_str.find(marker);
            if (pos != std::string::npos) {
                r.source_tsx = src_str.substr(pos + marker.size());
            } else {
                r.source_tsx = std::filesystem::path(src).filename().string();
            }
        }
        tilesets_.push_back(std::move(r));
        ts_walk_props.push_back(loadWalkableTileProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx));
        auto mon_props = loadTileStringProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx, "monster_name");
        ts_monster_props.push_back(std::move(mon_props));
        auto npc_props = loadTileStringProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx, "npc_name");
        ts_npc_props.push_back(std::move(npc_props));
        auto item_props = loadTileStringProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx, "item_name");
        if (item_props.empty()) {
            item_props = loadTileStringProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx, "item_id");
        }
        if (item_props.empty()) {
            item_props = loadTileStringProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx, "item");
        }
        if (item_props.empty()) {
            item_props = loadTileStringProps(std::filesystem::path("game/assets/art/") / tilesets_.back().source_tsx, "name");
        }
        ts_item_props.push_back(std::move(item_props));
    }

    // ---- layers (CSV only) ----
    for (XMLElement* layer = map->FirstChildElement("layer");
         layer;
         layer = layer->NextSiblingElement("layer")) {

        RoomLayer L;

        const char* lname = layer->Attribute("name");
        L.name = lname ? lname : "Layer";

        L.width = 0;
        L.height = 0;
        layer->QueryIntAttribute("width", &L.width);
        layer->QueryIntAttribute("height", &L.height);
        if (L.width <= 0 || L.height <= 0) return false;

        XMLElement* data = layer->FirstChildElement("data");
        if (!data) return false;

        const char* enc = data->Attribute("encoding");
        if (!enc || std::strcmp(enc, "csv") != 0) return false;

        const char* text = data->GetText();
        if (!text) return false;

        L.gids = parseCsvU32(text);
        if ((int)L.gids.size() != L.width * L.height) return false;

        layers_.push_back(std::move(L));
    }

    // ---- portal triggers (object layer named Portals) ----
    for (XMLElement* og = map->FirstChildElement("objectgroup");
         og;
         og = og->NextSiblingElement("objectgroup")) {
        const char* lname = og->Attribute("name");
        if (!lname || std::string(lname) != "Portals") continue;
        XMLElement* og_props = og->FirstChildElement("properties");

        for (XMLElement* obj = og->FirstChildElement("object");
             obj;
             obj = obj->NextSiblingElement("object")) {
            PortalTrigger p{};
            std::string to_world;
            obj->QueryFloatAttribute("x", &p.x);
            obj->QueryFloatAttribute("y", &p.y);
            obj->QueryFloatAttribute("width", &p.w);
            obj->QueryFloatAttribute("height", &p.h);

            bool has_room = false;
            bool has_to_x = false;
            bool has_to_y = false;
            auto parsePortalProps = [&](XMLElement* props) {
                if (!props) return;
                for (XMLElement* prop = props->FirstChildElement("property");
                     prop;
                     prop = prop->NextSiblingElement("property")) {
                    auto read_str = [&](XMLElement* el) -> std::string {
                        const char* v = el->Attribute("value");
                        if (!v) v = el->GetText();
                        return v ? v : "";
                    };
                    auto read_int = [&](XMLElement* el, int fallback) -> int {
                        int out = fallback;
                        if (el->QueryIntAttribute("value", &out) == XML_SUCCESS) return out;
                        const char* t = el->GetText();
                        try { return t ? std::stoi(t) : fallback; } catch (...) { return fallback; }
                    };

                    const char* n = prop->Attribute("name");
                    std::string key = n ? std::string(n) : "";

                    if (key == "to_room" || key == "room") {
                        p.to_room = read_str(prop);
                        has_room = !p.to_room.empty();
                    } else if (key == "to_world" || key == "world") {
                        to_world = read_str(prop);
                    } else if (key == "to_x" || key == "x") {
                        p.to_pos.x = read_int(prop, p.to_pos.x);
                        has_to_x = true;
                    } else if (key == "to_y" || key == "y") {
                        p.to_pos.y = read_int(prop, p.to_pos.y);
                        has_to_y = true;
                    }

                    // Support class properties like p1.room/x/y.
                    XMLElement* nested = prop->FirstChildElement("properties");
                    if (!nested) continue;
                    for (XMLElement* np = nested->FirstChildElement("property");
                         np;
                         np = np->NextSiblingElement("property")) {
                        const char* nn = np->Attribute("name");
                        if (!nn) continue;
                        std::string nkey = nn;
                        if (nkey == "to_room" || nkey == "room") {
                            p.to_room = read_str(np);
                            has_room = !p.to_room.empty();
                        } else if (nkey == "to_world" || nkey == "world") {
                            to_world = read_str(np);
                        } else if (nkey == "to_x" || nkey == "x") {
                            p.to_pos.x = read_int(np, p.to_pos.x);
                            has_to_x = true;
                        } else if (nkey == "to_y" || nkey == "y") {
                            p.to_pos.y = read_int(np, p.to_pos.y);
                            has_to_y = true;
                        }
                    }
                }
            };

            parsePortalProps(obj->FirstChildElement("properties"));
            if (!has_room || !has_to_x || !has_to_y) {
                parsePortalProps(og_props);
            }

            // Fallback to map-level class property "to" flattened as "to.room/to.x/to.y"
            if (!has_room) {
                auto it = props_.find("to.room");
                if (it != props_.end()) {
                    p.to_room = it->second;
                    has_room = !p.to_room.empty();
                }
            }
            if (!has_to_x) {
                auto it = props_.find("to.x");
                if (it != props_.end()) {
                    try { p.to_pos.x = std::stoi(it->second); } catch (...) {}
                }
            }
            if (!has_to_y) {
                auto it = props_.find("to.y");
                if (it != props_.end()) {
                    try { p.to_pos.y = std::stoi(it->second); } catch (...) {}
                }
            }

            if (!p.to_room.empty()) {
                if (!to_world.empty() && p.to_room.find(':') == std::string::npos) {
                    p.to_room = to_world + ":" + p.to_room;
                }
                portals_.push_back(std::move(p));
            }
        }
    }

    auto findTsIndexByGid = [&](uint32_t gid) -> int {
        int best = -1;
        for (size_t i = 0; i < tilesets_.size(); ++i) {
            if (gid >= static_cast<uint32_t>(tilesets_[i].first_gid)) best = static_cast<int>(i);
            else break;
        }
        return best;
    };

    // ---- monster spawns from tile layer named Monsters ----
    for (const auto& layer : layers_) {
        if (layer.name != "Monsters") continue;
        for (int y = 0; y < map_h_; ++y) {
            for (int x = 0; x < map_w_; ++x) {
                const uint32_t raw = layer.gids[(size_t)y * (size_t)layer.width + (size_t)x];
                if (raw == 0) continue;
                const uint32_t gid = raw & 0x1FFFFFFFu;
                const int tsi = findTsIndexByGid(gid);
                if (tsi < 0 || tsi >= static_cast<int>(ts_monster_props.size())) continue;
                const int local = static_cast<int>(gid) - tilesets_[tsi].first_gid;
                auto it = ts_monster_props[tsi].find(local);
                if (it == ts_monster_props[tsi].end() || it->second.empty()) continue;
                monster_spawns_.push_back(MonsterSpawn{it->second, "", {x, y}});
            }
        }
        break;
    }

    // ---- item spawns from tile layer named Items ----
    for (const auto& layer : layers_) {
        if (layer.name != "Items") continue;
        for (int y = 0; y < map_h_; ++y) {
            for (int x = 0; x < map_w_; ++x) {
                const uint32_t raw = layer.gids[(size_t)y * (size_t)layer.width + (size_t)x];
                if (raw == 0) continue;
                const uint32_t gid = raw & 0x1FFFFFFFu;
                const int tsi = findTsIndexByGid(gid);
                if (tsi < 0 || tsi >= static_cast<int>(ts_item_props.size())) continue;
                const int local = static_cast<int>(gid) - tilesets_[tsi].first_gid;
                auto it = ts_item_props[tsi].find(local);
                if (it == ts_item_props[tsi].end() || it->second.empty()) continue;
                item_spawns_.push_back(ItemSpawn{it->second, {x, y}});
            }
        }
        break;
    }

    // ---- npc spawns from tile layer named NPCs ----
    for (const auto& layer : layers_) {
        if (layer.name != "NPCs") continue;
        for (int y = 0; y < map_h_; ++y) {
            for (int x = 0; x < map_w_; ++x) {
                const uint32_t raw = layer.gids[(size_t)y * (size_t)layer.width + (size_t)x];
                if (raw == 0) continue;
                const uint32_t gid = raw & 0x1FFFFFFFu;
                const int tsi = findTsIndexByGid(gid);
                if (tsi < 0 || tsi >= static_cast<int>(ts_npc_props.size())) continue;
                const int local = static_cast<int>(gid) - tilesets_[tsi].first_gid;
                auto it = ts_npc_props[tsi].find(local);
                if (it == ts_npc_props[tsi].end() || it->second.empty()) continue;
                npc_spawns_.push_back(NpcSpawn{it->second, {x, y}});
            }
        }
        break;
    }

    // require at least one tileset + one layer like before
    if (tilesets_.empty()) return false;
    if (layers_.empty()) return false;

    // Default semantics: everything is walkable unless explicitly blocked.
    walkable_mask_.assign((size_t)map_w_ * (size_t)map_h_, 1);

    // Block layer convention: gid != 0 blocks movement.
    for (const auto& layer : layers_) {
        if (layer.name != "Block") continue;
        for (int y = 0; y < map_h_; ++y) {
            for (int x = 0; x < map_w_; ++x) {
                const uint32_t gid = layer.gids[(size_t)y * (size_t)layer.width + (size_t)x];
                if (gid != 0) {
                    walkable_mask_[(size_t)y * (size_t)map_w_ + (size_t)x] = 0;
                }
            }
        }
    }

    // Tile property walkability resolves from top-most painted tile downward.
    // This lets a walkable overlay tile (for example, a bridge) override a
    // non-walkable base tile beneath it.
    for (int y = 0; y < map_h_; ++y) {
        for (int x = 0; x < map_w_; ++x) {
            const size_t idx = (size_t)y * (size_t)map_w_ + (size_t)x;
            if (walkable_mask_[idx] == 0) continue; // explicit Block layer remains authoritative

            for (int li = static_cast<int>(layers_.size()) - 1; li >= 0; --li) {
                const auto& layer = layers_[static_cast<size_t>(li)];
                if (layer.name == "Block") continue;
                if (isCollisionMetadataLayerName(layer.name)) continue;

                const uint32_t raw = layer.gids[(size_t)y * (size_t)layer.width + (size_t)x];
                if (raw == 0) continue;
                const uint32_t gid = raw & 0x1FFFFFFFu; // strip TMX flip flags

                const int tsi = findTsIndexByGid(gid);
                if (tsi < 0 || tsi >= static_cast<int>(ts_walk_props.size())) {
                    walkable_mask_[idx] = 1;
                    break;
                }

                const int local = static_cast<int>(gid) - tilesets_[tsi].first_gid;
                auto it = ts_walk_props[tsi].find(local);
                if (it == ts_walk_props[tsi].end()) {
                    walkable_mask_[idx] = 1;
                    break;
                }

                walkable_mask_[idx] = it->second ? 1 : 0;
                break;
            }
        }
    }

    return true;
}

bool Room::isWalkable(int x, int y) const {
    if (x < 0 || y < 0 || x >= map_w_ || y >= map_h_) return false;
    if (walkable_mask_.empty()) return true;
    return walkable_mask_[(size_t)y * (size_t)map_w_ + (size_t)x] != 0;
}
