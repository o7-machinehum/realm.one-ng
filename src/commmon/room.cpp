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
    props_.clear(); // <-- your new dict member (e.g. std::unordered_map<std::string,std::string>)

    XMLElement* map = doc.FirstChildElement("map");
    if (!map) return false;

    map->QueryIntAttribute("width", &map_w_);
    map->QueryIntAttribute("height", &map_h_);
    map->QueryIntAttribute("tilewidth", &tile_w_);
    map->QueryIntAttribute("tileheight", &tile_h_);

    // ---- properties -> flat dict (dot-path keys) ----
    auto addPropFlat = [&](const std::string& key, const char* v) {
        props_[key] = v ? v : "";
    };

    std::function<void(XMLElement*, const std::string&)> walkProps =
        [&](XMLElement* propsElem, const std::string& prefix) {
            if (!propsElem) return;

            for (XMLElement* p = propsElem->FirstChildElement("property");
                 p;
                 p = p->NextSiblingElement("property")) {

                const char* name = p->Attribute("name");
                if (!name) continue;

                std::string key = prefix.empty() ? std::string(name)
                                                 : (prefix + "." + name);

                const char* type = p->Attribute("type");
                if (type && std::string(type) == "class") {
                    walkProps(p->FirstChildElement("properties"), key);
                    continue;
                }

                const char* v = p->Attribute("value");
                if (!v) v = p->GetText();
                addPropFlat(key, v);
            }
        };

    walkProps(map->FirstChildElement("properties"), "");

    // ---- tilesets ----
    for (XMLElement* ts = map->FirstChildElement("tileset"); ts; ts = ts->NextSiblingElement("tileset")) {
        RoomTilesetRef r;
        ts->QueryIntAttribute("firstgid", &r.first_gid);
        const char* src = ts->Attribute("source");
        if (!src) return false;
        r.source_tsx = src;
        tilesets_.push_back(std::move(r));
    }
    if (tilesets_.empty()) return false;

    // ---- layers (CSV only) ----
    for (XMLElement* layer = map->FirstChildElement("layer"); layer; layer = layer->NextSiblingElement("layer")) {
        RoomLayer L;
        const char* name = layer->Attribute("name");
        L.name = name ? name : "Layer";

        layer->QueryIntAttribute("width", &L.width);
        layer->QueryIntAttribute("height", &L.height);

        XMLElement* data = layer->FirstChildElement("data");
        if (!data) return false;

        const char* enc = data->Attribute("encoding");
        if (!enc || std::string(enc) != "csv") return false;

        L.gids = parseCsvU32(data->GetText());
        if ((int)L.gids.size() != L.width * L.height) return false;

        layers_.push_back(std::move(L));
    }

    return !layers_.empty();
}
