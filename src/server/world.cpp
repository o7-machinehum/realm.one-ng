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

std::optional<std::pair<int, int>> parseGridPos(const std::string& world_name) {
    static const std::regex re("^(-?[0-9]+)_(-?[0-9]+)$");
    std::smatch m;
    if (!std::regex_match(world_name, m, re)) return std::nullopt;
    try {
        return std::pair<int, int>{std::stoi(m[1]), std::stoi(m[2])};
    } catch (...) {
        return std::nullopt;
    }
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
    if (!fs::exists(dir) || !fs::is_directory(dir)) return;

    // Prefer data-driven world files when present, including nested world dirs.
    std::vector<fs::path> world_files;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".world") {
            if (entry.path().stem() == "globe") continue;
            world_files.push_back(entry.path());
        }
    }

    if (!world_files.empty()) {
        std::sort(world_files.begin(), world_files.end());
        const bool has_nested_world = std::any_of(world_files.begin(), world_files.end(), [&](const fs::path& p) {
            return p.parent_path() != dir;
        });

        std::unordered_map<std::string, fs::path> preferred_by_parent;
        for (const auto& wf : world_files) {
            const fs::path parent = wf.parent_path();
            if (parent == dir) continue;
            if (wf.stem() == parent.filename()) {
                preferred_by_parent[parent.string()] = wf;
            }
        }

        for (const auto& wf : world_files) {
            const fs::path parent = wf.parent_path();
            if (has_nested_world && parent == dir) continue;
            auto pit = preferred_by_parent.find(parent.string());
            if (pit != preferred_by_parent.end() && pit->second != wf) continue;

            std::string inferred_name = wf.stem().string();
            if (parent != dir && !parent.filename().empty()) {
                inferred_name = parent.filename().string();
            }
            if (auto gp = parseGridPos(inferred_name); gp.has_value()) {
                world_grid_pos_[inferred_name] = *gp;
            }
            loadWorldFile(wf.string(), inferred_name);
        }
        return;
    }

    // Fallback: raw TMX directory mode.
    const std::string inferred_world_name =
        dir.filename().empty() ? default_world_name_ : dir.filename().string();
    for (const auto& entry : fs::recursive_directory_iterator(world_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".tmx") continue;

        const std::string fname = entry.path().string();
        std::cout << "Loading File: " << fname << std::endl;

        auto room = std::make_unique<Room>();
        if (!room->loadFromFile(fname)) continue;

        Placement pl;
        pl.world_name = inferred_world_name;
        pl.room_name = inferred_world_name + ":" + room->get_name();
        pl.tile_w = room->tile_width();
        pl.tile_h = room->tile_height();
        pl.map_w = room->map_width();
        pl.map_h = room->map_height();
        addAlias(first_room_alias_, room->get_name(), pl.room_name);
        _world[pl.room_name] = std::move(room);
        placements_[pl.room_name] = std::move(pl);
        if (entry.path().filename() == "0.tmx") {
            world_level0_room_[inferred_world_name] = pl.room_name;
        }
        if (default_room_name_.empty() || default_room_name_ == "0.tmx") default_room_name_ = _world.begin()->first;
        if (default_world_name_ == "floor") default_world_name_ = inferred_world_name;
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
    if (default_world_name_ == "floor") default_world_name_ = effective_world_name;
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
        if (map_path.extension() != ".tmx") continue;
        std::cout << "Loading File: " << map_path.string() << std::endl;

        auto room = std::make_unique<Room>();
        if (!room->loadFromFile(map_path)) continue;

        const std::string base_name = room->get_name();
        const std::string qualified_name = effective_world_name + ":" + base_name;

        Placement pl;
        pl.world_name = effective_world_name;
        pl.room_name = qualified_name;
        pl.tile_w = room->tile_width();
        pl.tile_h = room->tile_height();
        pl.map_w = room->map_width();
        pl.map_h = room->map_height();

        addAlias(first_room_alias_, base_name, qualified_name);
        addAlias(first_room_alias_, effective_world_name + ":" + base_name, qualified_name);
        if (base_name == "0.tmx") {
            world_level0_room_[effective_world_name] = qualified_name;
        }
        if (auto gp = parseGridPos(effective_world_name); gp.has_value()) {
            world_grid_pos_[effective_world_name] = *gp;
        }

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

    // Position-coded worlds: only level-0 is allowed to edge-transition across worlds.
    const auto gp_it = world_grid_pos_.find(cur.world_name);
    if (gp_it != world_grid_pos_.end()) {
        if (cur_room->get_name() != "0.tmx") return false;

        int target_gx = gp_it->second.first;
        int target_gy = gp_it->second.second;
        if (attempted_x < 0) {
            target_gx -= 1;
        } else if (attempted_x >= cur.map_w) {
            target_gx += 1;
        } else if (attempted_y < 0) {
            target_gy += 1;
        } else if (attempted_y >= cur.map_h) {
            target_gy -= 1;
        }

        const std::string target_world =
            std::to_string(target_gx) + "_" + std::to_string(target_gy);
        auto l0_it = world_level0_room_.find(target_world);
        if (l0_it == world_level0_room_.end()) return false;

        auto pl_it = placements_.find(l0_it->second);
        if (pl_it == placements_.end()) return false;
        const Placement& dst = pl_it->second;

        out_room = l0_it->second;
        if (attempted_x < 0) {
            out_x = std::max(0, dst.map_w - 1);
            out_y = clampInt(attempted_y, 0, std::max(0, dst.map_h - 1));
        } else if (attempted_x >= cur.map_w) {
            out_x = 0;
            out_y = clampInt(attempted_y, 0, std::max(0, dst.map_h - 1));
        } else if (attempted_y < 0) {
            out_x = clampInt(attempted_x, 0, std::max(0, dst.map_w - 1));
            out_y = std::max(0, dst.map_h - 1);
        } else {
            out_x = clampInt(attempted_x, 0, std::max(0, dst.map_w - 1));
            out_y = 0;
        }
        return true;
    }

    return false;
}
