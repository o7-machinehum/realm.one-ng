#include "room.h"
#include "sprites.h"

#include <tinyxml2.h>

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

static std::vector<int> parseCsvInts(const char* text) {
    std::vector<int> out;
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
            out.push_back(std::stoi(token));
        } catch (...) {
            // If Tiled ever leaves trailing junk, just fail soft.
            // You can make this strict later.
        }
    }
    return out;
}

static std::filesystem::path absPathNorm(const std::filesystem::path& p) {
    try {
        return std::filesystem::weakly_canonical(p);
    } catch (...) {
        return std::filesystem::absolute(p);
    }
}

// ---------- Room impl ----------
bool Room::loadFromFile(const std::filesystem::path& tmxPath) {
    const auto absTmx = absPathNorm(tmxPath);
    const std::string xml = readFileToString(absTmx);
    if (xml.empty()) {
        std::cerr << "TMX read failed: " << absTmx << "\n";
        return false;
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "TMX parse failed: " << absTmx << "\n";
        return false;
    }

    return parseTmxDoc(doc, absTmx.parent_path());
}

bool Room::loadFromXmlString(const std::string& tmxXml, const std::filesystem::path& baseDir) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(tmxXml.c_str(), tmxXml.size()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "TMX parse failed from string\n";
        return false;
    }
    return parseTmxDoc(doc, absPathNorm(baseDir));
}

bool Room::parseTmxDoc(tinyxml2::XMLDocument& doc, const std::filesystem::path& baseDir) {
    layers_.clear();
    tsxAbsPath_.clear();

    auto* map = doc.FirstChildElement("map");
    if (!map) {
        std::cerr << "TMX missing <map>\n";
        return false;
    }

    map->QueryIntAttribute("width", &mapW_);
    map->QueryIntAttribute("height", &mapH_);
    map->QueryIntAttribute("tilewidth", &tileW_);
    map->QueryIntAttribute("tileheight", &tileH_);

    // Single tileset ref (for now)
    auto* ts = map->FirstChildElement("tileset");
    if (!ts) {
        std::cerr << "TMX missing <tileset>\n";
        return false;
    }

    ts->QueryIntAttribute("firstgid", &firstGid_);
    const char* tsxRel = ts->Attribute("source");
    if (!tsxRel) {
        std::cerr << "TMX <tileset> missing source=\"...tsx\"\n";
        return false;
    }

    tsxAbsPath_ = absPathNorm(baseDir / tsxRel);

    // Layers (CSV only)
    for (auto* layer = map->FirstChildElement("layer"); layer; layer = layer->NextSiblingElement("layer")) {
        RoomLayer L;

        const char* name = layer->Attribute("name");
        L.name = name ? name : "Layer";

        layer->QueryIntAttribute("width", &L.width);
        layer->QueryIntAttribute("height", &L.height);

        auto* data = layer->FirstChildElement("data");
        if (!data) {
            std::cerr << "Layer '" << L.name << "' missing <data>\n";
            return false;
        }

        const char* enc = data->Attribute("encoding");
        if (!enc || std::string(enc) != "csv") {
            std::cerr << "Layer '" << L.name << "': only encoding=\"csv\" supported\n";
            return false;
        }

        L.gids = parseCsvInts(data->GetText());
        if ((int)L.gids.size() != L.width * L.height) {
            std::cerr << "Layer '" << L.name << "': CSV count mismatch (got "
                      << L.gids.size() << ", expected " << (L.width * L.height) << ")\n";
            return false;
        }

        layers_.push_back(std::move(L));
    }

    return true;
}

void Room::draw(const SpriteAtlas& atlas, Vector2 originPx) const {
    const SpriteTileset* ts = atlas.findByTsxPath(tsxAbsPath_);
    if (!ts || !ts->loaded) return;

    // NOTE: This ignores Tiled flip flags (high bits). Add later when needed.
    for (const auto& layer : layers_) {
        for (int y = 0; y < layer.height; y++) {
            for (int x = 0; x < layer.width; x++) {
                const int gid = layer.gids[y * layer.width + x];
                if (gid == 0) continue;

                const int localId = gid - firstGid_;
                if (localId < 0) continue;

                const int sx = (localId % ts->columns) * ts->tileW;
                const int sy = (localId / ts->columns) * ts->tileH;

                Rectangle src{
                    (float)sx,
                    (float)sy,
                    (float)ts->tileW,
                    (float)ts->tileH
                };

                constexpr float kScale = 2.0f;

                Rectangle dst{
                    originPx.x + (float)(x * tileW_) * kScale,
                    originPx.y + (float)(y * tileH_) * kScale,
                    (float)tileW_ * kScale,
                    (float)tileH_ * kScale
                };

                DrawTexturePro(ts->texture, src, dst, Vector2{0, 0}, 0.0f, WHITE);
            }
        }
    }
}
