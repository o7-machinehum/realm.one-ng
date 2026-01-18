#include "room.h"

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

// ---------- binary pack/unpack (little-endian) ----------
static void wr_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
}

static bool rd_u32(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    if ((size_t)(end - p) < 4) return false;
    out = (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
    p += 4;
    return true;
}

static void wr_str(std::vector<uint8_t>& b, const std::string& s) {
    wr_u32(b, (uint32_t)s.size());
    b.insert(b.end(), s.begin(), s.end());
}

static bool rd_str(const uint8_t*& p, const uint8_t* end, std::string& out) {
    uint32_t n = 0;
    if (!rd_u32(p, end, n)) return false;
    if ((size_t)(end - p) < n) return false;
    out.assign((const char*)p, (size_t)n);
    p += n;
    return true;
}

// ---------- Room impl ----------
bool Room::loadFromFile(const std::filesystem::path& tmx_path) {
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

    XMLElement* map = doc.FirstChildElement("map");
    if (!map) return false;

    map->QueryIntAttribute("width", &map_w_);
    map->QueryIntAttribute("height", &map_h_);
    map->QueryIntAttribute("tilewidth", &tile_w_);
    map->QueryIntAttribute("tileheight", &tile_h_);

    // tilesets
    for (XMLElement* ts = map->FirstChildElement("tileset"); ts; ts = ts->NextSiblingElement("tileset")) {
        RoomTilesetRef r;
        ts->QueryIntAttribute("firstgid", &r.first_gid);
        const char* src = ts->Attribute("source");
        if (!src) return false;
        r.source_tsx = src;
        tilesets_.push_back(std::move(r));
    }
    if (tilesets_.empty()) return false;

    // layers (CSV only)
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

// ---------- serialization ----------
std::vector<uint8_t> Room::serialize() const {
    std::vector<uint8_t> b;
    b.reserve(64 + layers_.size() * 64);

    // magic + version
    b.push_back('R'); b.push_back('M'); b.push_back('0'); b.push_back('1');

    wr_u32(b, (uint32_t)map_w_);
    wr_u32(b, (uint32_t)map_h_);
    wr_u32(b, (uint32_t)tile_w_);
    wr_u32(b, (uint32_t)tile_h_);

    // tilesets
    wr_u32(b, (uint32_t)tilesets_.size());
    for (const auto& ts : tilesets_) {
        wr_u32(b, (uint32_t)ts.first_gid);
        wr_str(b, ts.source_tsx);
    }

    // layers
    wr_u32(b, (uint32_t)layers_.size());
    for (const auto& L : layers_) {
        wr_str(b, L.name);
        wr_u32(b, (uint32_t)L.width);
        wr_u32(b, (uint32_t)L.height);

        wr_u32(b, (uint32_t)L.gids.size());
        for (uint32_t g : L.gids) wr_u32(b, g);
    }

    return b;
}

bool Room::deserialize(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;

    const uint8_t* p = data;
    const uint8_t* end = data + size;

    // magic
    if ((end - p) < 4) return false;
    if (p[0] != 'R' || p[1] != 'M' || p[2] != '0' || p[3] != '1') return false;
    p += 4;

    uint32_t mw=0, mh=0, tw=0, th=0;
    if (!rd_u32(p, end, mw)) return false;
    if (!rd_u32(p, end, mh)) return false;
    if (!rd_u32(p, end, tw)) return false;
    if (!rd_u32(p, end, th)) return false;

    map_w_ = (int)mw;
    map_h_ = (int)mh;
    tile_w_ = (int)tw;
    tile_h_ = (int)th;

    tilesets_.clear();
    layers_.clear();

    uint32_t ts_count = 0;
    if (!rd_u32(p, end, ts_count)) return false;
    tilesets_.reserve(ts_count);

    for (uint32_t i = 0; i < ts_count; i++) {
        RoomTilesetRef ts;
        uint32_t fg = 0;
        if (!rd_u32(p, end, fg)) return false;
        ts.first_gid = (int)fg;
        if (!rd_str(p, end, ts.source_tsx)) return false;
        tilesets_.push_back(std::move(ts));
    }

    uint32_t layer_count = 0;
    if (!rd_u32(p, end, layer_count)) return false;
    layers_.reserve(layer_count);

    for (uint32_t i = 0; i < layer_count; i++) {
        RoomLayer L;
        if (!rd_str(p, end, L.name)) return false;

        uint32_t w=0,h=0;
        if (!rd_u32(p, end, w)) return false;
        if (!rd_u32(p, end, h)) return false;
        L.width = (int)w;
        L.height = (int)h;

        uint32_t n = 0;
        if (!rd_u32(p, end, n)) return false;
        L.gids.resize(n);

        for (uint32_t j = 0; j < n; j++) {
            uint32_t g = 0;
            if (!rd_u32(p, end, g)) return false;
            L.gids[j] = g;
        }

        if ((int)L.gids.size() != L.width * L.height) return false;

        layers_.push_back(std::move(L));
    }

    return true;
}

