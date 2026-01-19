#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace tinyxml2 { class XMLDocument; }

struct RoomTilesetRef {
    int first_gid = 1;
    std::string source_tsx; // tsx fname
};

struct RoomLayer {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> gids; // size = width*height
};

class Room {
public:
    bool loadFromFile(const std::filesystem::path& tmx_path);

    // ---- data access ----
    int map_width()  const { return map_w_; }
    int map_height() const { return map_h_; }
    int tile_width() const { return tile_w_; }
    int tile_height() const { return tile_h_; }

    const std::vector<RoomTilesetRef>& tilesets() const { return tilesets_; }
    const std::vector<RoomLayer>& layers() const { return layers_; }

    std::string get_name() {
        return name_;
    }

private:
    bool parseTmxDoc(tinyxml2::XMLDocument& doc);

    int map_w_ = 0;
    int map_h_ = 0;
    int tile_w_ = 16;
    int tile_h_ = 16;

    std::string name_; // Name of room inc. path
    std::vector<RoomTilesetRef> tilesets_;
    std::vector<RoomLayer> layers_;
    std::unordered_map<std::string, std::string> props_;
};
