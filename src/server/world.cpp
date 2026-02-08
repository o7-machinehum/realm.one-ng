#include "world.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string readAllText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

std::string addTmxExt(std::string s) {
    if (s.size() >= 4 && s.substr(s.size() - 4) == ".tmx") return s;
    return s + ".tmx";
}

void addAlias(std::unordered_map<std::string, std::string>& aliases,
              const std::string& raw_alias,
              const std::string& canonical) {
    if (raw_alias.empty()) return;
    aliases.emplace(raw_alias, canonical);

    const std::string with_ext = addTmxExt(raw_alias);
    aliases.emplace(with_ext, canonical);
}

} // namespace

World::World(std::string source) {
    const fs::path p(source);
    if (p.extension() == ".world") {
        loadWorldFile(source, p.stem().string());
    } else {
        loadDirectory(source);
    }

    if (!_world.empty() && _world.find(default_room_name_) == _world.end()) {
        default_room_name_ = _world.begin()->first;
    }
}

void World::loadDirectory(const std::string& world_dir) {
    const fs::path dir(world_dir);

    // Prefer data-driven world files when present.
    std::vector<fs::path> world_files;
    if (fs::exists(dir) && fs::is_directory(dir)) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".world") {
                world_files.push_back(entry.path());
            }
        }
    }

    if (!world_files.empty()) {
        std::sort(world_files.begin(), world_files.end());
        for (const auto& wf : world_files) {
            loadWorldFile(wf.string(), wf.stem().string());
        }
        return;
    }

    // Fallback: raw TMX directory mode.
    for (const auto& entry : fs::directory_iterator(world_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".tmx") continue;

        const std::string fname = entry.path().string();
        std::cout << "Loading File: " << fname << std::endl;

        auto room = std::make_unique<Room>();
        if (!room->loadFromFile(fname)) continue;

        Placement pl;
        pl.world_name = default_world_name_;
        pl.room_name = default_world_name_ + ":" + room->get_name();
        pl.world_x = 0;
        pl.world_y = 0;
        pl.tile_w = room->tile_width();
        pl.tile_h = room->tile_height();
        pl.map_w = room->map_width();
        pl.map_h = room->map_height();
        pl.pixel_w = pl.map_w * pl.tile_w;
        pl.pixel_h = pl.map_h * pl.tile_h;

        addAlias(first_room_alias_, room->get_name(), pl.room_name);
        _world[pl.room_name] = std::move(room);
        placements_[pl.room_name] = std::move(pl);
        if (default_room_name_.empty() || default_room_name_ == "d1.tmx") default_room_name_ = _world.begin()->first;
    }
}

int World::findIntField(const std::string& text, const std::string& key, int fallback) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        try {
            return std::stoi(m[1]);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::string World::findStringField(const std::string& text, const std::string& key) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        return m[1];
    }
    return {};
}

void World::loadWorldFile(const std::string& world_file, const std::string& world_name) {
    const fs::path wp(world_file);
    const std::string json = readAllText(wp);
    if (json.empty()) return;

    std::string effective_world_name = world_name;
    if (const std::string floor_prop = findStringField(json, "floor"); !floor_prop.empty()) {
        effective_world_name = floor_prop;
    } else {
        const int floor_num = findIntField(json, "floor", -999999);
        if (floor_num != -999999) effective_world_name = std::to_string(floor_num);
    }
    std::regex map_obj_re("\\{[^{}]*\\\"fileName\\\"[^{}]*\\}");
    auto begin = std::sregex_iterator(json.begin(), json.end(), map_obj_re);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const std::string obj = it->str();
        const std::string rel_file = findStringField(obj, "fileName");
        if (rel_file.empty()) continue;

        fs::path map_path = wp.parent_path() / rel_file;
        if (!fs::exists(map_path)) {
            fs::path alt = wp.parent_path().parent_path() / rel_file;
            if (fs::exists(alt)) map_path = alt;
        }
        std::cout << "Loading File: " << map_path.string() << std::endl;

        auto room = std::make_unique<Room>();
        if (!room->loadFromFile(map_path)) continue;

        const std::string base_name = room->get_name();
        const std::string qualified_name = effective_world_name + ":" + base_name;

        Placement pl;
        pl.world_name = effective_world_name;
        pl.room_name = qualified_name;
        pl.world_x = findIntField(obj, "x", 0);
        pl.world_y = findIntField(obj, "y", 0);
        pl.pixel_w = findIntField(obj, "width", 0);
        pl.pixel_h = findIntField(obj, "height", 0);
        pl.tile_w = room->tile_width();
        pl.tile_h = room->tile_height();
        pl.map_w = room->map_width();
        pl.map_h = room->map_height();

        if (pl.pixel_w <= 0) pl.pixel_w = pl.map_w * pl.tile_w;
        if (pl.pixel_h <= 0) pl.pixel_h = pl.map_h * pl.tile_h;

        addAlias(first_room_alias_, base_name, qualified_name);
        addAlias(first_room_alias_, effective_world_name + ":" + base_name, qualified_name);

        for (const std::string key : {"room", "id", "name", "room_id", "room_name", "alias"}) {
            auto it = room->properties().find(key);
            if (it == room->properties().end()) continue;
            addAlias(first_room_alias_, it->second, qualified_name);
            addAlias(first_room_alias_, effective_world_name + ":" + it->second, qualified_name);
        }
        _world[qualified_name] = std::move(room);
        placements_[qualified_name] = std::move(pl);

        if (default_room_name_.empty() || _world.size() == 1) {
            default_room_name_ = qualified_name;
        }
    }
}

std::optional<std::string> World::onlyRoomInWorld(const std::string& world_name) const {
    std::optional<std::string> found;
    for (const auto& [room_name, pl] : placements_) {
        if (pl.world_name != world_name) continue;
        if (found.has_value()) return std::nullopt;
        found = room_name;
    }
    return found;
}

std::string World::resolveRoomName(const std::string& raw, const std::string& current_room) const {
    if (raw.empty()) return {};

    auto has_colon = raw.find(':') != std::string::npos;
    std::string normalized = raw;

    if (has_colon) {
        auto a = first_room_alias_.find(normalized);
        if (a != first_room_alias_.end()) return a->second;

        const auto pos = normalized.find(':');
        const std::string world_name = normalized.substr(0, pos);
        const std::string raw_part = normalized.substr(pos + 1);
        const std::string room_part = addTmxExt(raw_part);
        const std::string key = world_name + ":" + room_part;
        if (_world.find(key) != _world.end()) return key;

        auto b = first_room_alias_.find(key);
        if (b != first_room_alias_.end()) return b->second;

        auto c = first_room_alias_.find(world_name + ":" + raw_part);
        if (c != first_room_alias_.end()) return c->second;

        if (auto only = onlyRoomInWorld(world_name); only.has_value()) return *only;
        return {};
    }

    auto a = first_room_alias_.find(normalized);
    if (a != first_room_alias_.end()) return a->second;

    // Prefer current world for unqualified destinations.
    const auto cur_pos = current_room.find(':');
    if (cur_pos != std::string::npos) {
        const std::string cur_world = current_room.substr(0, cur_pos);
        const std::string key = cur_world + ":" + addTmxExt(normalized);
        if (_world.find(key) != _world.end()) return key;
    }

    // Fall back to first known alias for that bare room name.
    normalized = addTmxExt(normalized);
    auto it = first_room_alias_.find(normalized);
    if (it != first_room_alias_.end()) return it->second;

    // Finally try default world.
    const std::string def_key = default_world_name_ + ":" + normalized;
    if (_world.find(def_key) != _world.end()) return def_key;

    return {};
}

const Room* World::getRoom(const std::string& name) const {
    auto it = _world.find(name);
    if (it != _world.end()) return it->second.get();

    const std::string resolved = resolveRoomName(name, {});
    if (resolved.empty()) return nullptr;
    auto it2 = _world.find(resolved);
    return (it2 == _world.end()) ? nullptr : it2->second.get();
}

const Room* World::defaultRoom() const {
    return getRoom(default_room_name_);
}

std::vector<std::string> World::roomNames() const {
    std::vector<std::string> names;
    names.reserve(_world.size());
    for (const auto& [name, _] : _world) names.push_back(name);
    return names;
}

bool World::resolveEdgeTransition(const std::string& current_room,
                                  int attempted_x,
                                  int attempted_y,
                                  std::string& out_room,
                                  int& out_x,
                                  int& out_y) const {
    const std::string cur_key = resolveRoomName(current_room, current_room);
    const Room* cur_room = getRoom(cur_key);
    if (!cur_room) return false;

    auto it_cur = placements_.find(cur_key);
    if (it_cur == placements_.end()) return false;

    const Placement& cur = it_cur->second;

    if (attempted_x >= 0 && attempted_x < cur.map_w && attempted_y >= 0 && attempted_y < cur.map_h) {
        out_room = cur_key;
        out_x = attempted_x;
        out_y = attempted_y;
        return true;
    }

    const int world_px = cur.world_x + attempted_x * cur.tile_w;
    const int world_py = cur.world_y + attempted_y * cur.tile_h;

    for (const auto& [name, pl] : placements_) {
        if (pl.world_name != cur.world_name) continue;
        if (pl.pixel_w <= 0 || pl.pixel_h <= 0) continue;

        const bool inside_x = (world_px >= pl.world_x) && (world_px < pl.world_x + pl.pixel_w);
        const bool inside_y = (world_py >= pl.world_y) && (world_py < pl.world_y + pl.pixel_h);
        if (!inside_x || !inside_y) continue;

        int tx = (world_px - pl.world_x) / std::max(1, pl.tile_w);
        int ty = (world_py - pl.world_y) / std::max(1, pl.tile_h);

        tx = clampInt(tx, 0, std::max(0, pl.map_w - 1));
        ty = clampInt(ty, 0, std::max(0, pl.map_h - 1));

        out_room = name;
        out_x = tx;
        out_y = ty;
        return true;
    }

    return false;
}
