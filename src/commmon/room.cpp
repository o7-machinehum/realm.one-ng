#include "room.h"
#include "helpers.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
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
    for (XMLElement* ts = map->FirstChildElement("tileset");
         ts;
         ts = ts->NextSiblingElement("tileset")) {

        RoomTilesetRef r;
        r.first_gid = 0;
        ts->QueryIntAttribute("firstgid", &r.first_gid);

        const char* src = ts->Attribute("source");
        if (!src || r.first_gid <= 0) return false;

        r.source_tsx = src;
        tilesets_.push_back(std::move(r));
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

    // require at least one tileset + one layer like before
    if (tilesets_.empty()) return false;
    if (layers_.empty()) return false;

    return true;
}
