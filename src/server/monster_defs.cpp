#include "monster_defs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string stripComment(const std::string& s) {
    bool in_quotes = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') in_quotes = !in_quotes;
        if (!in_quotes && s[i] == '#') return s.substr(0, i);
    }
    return s;
}

bool parseInt(const std::string& raw, int& out) {
    try {
        size_t pos = 0;
        const int v = std::stoi(raw, &pos);
        if (pos != raw.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseFloat(const std::string& raw, float& out) {
    try {
        size_t pos = 0;
        const float v = std::stof(raw, &pos);
        if (pos != raw.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::string parseStringValue(const std::string& raw) {
    std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

bool parseSize(const std::string& raw, int& out_w, int& out_h) {
    std::string v = parseStringValue(raw);
    const auto x = v.find('x');
    if (x == std::string::npos) return false;
    int w = 0;
    int h = 0;
    if (!parseInt(trim(v.substr(0, x)), w)) return false;
    if (!parseInt(trim(v.substr(x + 1)), h)) return false;
    out_w = std::max(1, w);
    out_h = std::max(1, h);
    return true;
}

bool parseTomlSubsetFile(const fs::path& p, MonsterDef& out) {
    std::ifstream in(p);
    if (!in) return false;

    MonsterDropDef current_drop{};
    bool in_drop_table = false;
    bool current_drop_valid = false;
    auto flush_drop = [&]() {
        if (!in_drop_table || !current_drop_valid) return;
        if (current_drop.item_id.empty()) return;
        current_drop.chance = std::max(0.0f, std::min(1.0f, current_drop.chance));
        out.drops.push_back(current_drop);
    };

    std::string line;
    while (std::getline(in, line)) {
        line = trim(stripComment(line));
        if (line.empty()) continue;

        if (line == "[[drops]]") {
            flush_drop();
            current_drop = MonsterDropDef{};
            in_drop_table = true;
            current_drop_valid = true;
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            flush_drop();
            in_drop_table = false;
            current_drop_valid = false;
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string raw_val = trim(line.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (in_drop_table) {
            if (key == "item" || key == "item_id") {
                current_drop.item_id = parseStringValue(raw_val);
            } else if (key == "chance" || key == "probability") {
                float v = current_drop.chance;
                if (parseFloat(raw_val, v)) current_drop.chance = v;
            }
            continue;
        }

        if (key == "name") out.name = parseStringValue(raw_val);
        else if (key == "sprite_tileset") out.sprite_tileset = parseStringValue(raw_val);
        else if (key == "monster_size" || key == "size") {
            int w = out.monster_size_w;
            int h = out.monster_size_h;
            if (parseSize(raw_val, w, h)) {
                out.monster_size_w = w;
                out.monster_size_h = h;
            }
        }
        else if (key == "max_hp") { int v = out.max_hp; if (parseInt(raw_val, v)) out.max_hp = std::max(1, v); }
        else if (key == "strength") { int v = out.strength; if (parseInt(raw_val, v)) out.strength = std::max(1, v); }
        else if (key == "speed_ms") { int v = out.speed_ms; if (parseInt(raw_val, v)) out.speed_ms = std::max(1, v); }
        else if (key == "exp_reward") { int v = out.exp_reward; if (parseInt(raw_val, v)) out.exp_reward = std::max(0, v); }
    }

    flush_drop();
    return true;
}

MonsterDef fallbackDefFromId(const std::string& id) {
    MonsterDef d;
    d.id = id;
    d.name = id;
    d.sprite_tileset = "character.tsx";
    return d;
}

} // namespace

std::vector<MonsterDef> loadMonsterDefs(const std::string& dir_path) {
    std::vector<MonsterDef> defs;
    const fs::path dir(dir_path);
    if (!fs::exists(dir) || !fs::is_directory(dir)) return defs;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".toml") continue;

        MonsterDef d = fallbackDefFromId(entry.path().stem().string());
        if (!parseTomlSubsetFile(entry.path(), d)) {
            std::cerr << "[server] failed to parse monster def: " << entry.path().string() << "\n";
            continue;
        }

        if (d.name.empty()) d.name = d.id;
        if (d.sprite_tileset.empty()) d.sprite_tileset = "character.tsx";

        defs.push_back(std::move(d));
    }

    std::sort(defs.begin(), defs.end(), [](const MonsterDef& a, const MonsterDef& b) {
        return a.id < b.id;
    });
    return defs;
}
