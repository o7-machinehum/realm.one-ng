#include "item_defs.h"

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

std::string parseStringValue(const std::string& raw) {
    std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

bool parseBool(const std::string& raw, bool& out) {
    std::string v = parseStringValue(raw);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parseTomlSubsetFile(const fs::path& p, ItemDef& out) {
    std::ifstream in(p);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(stripComment(line));
        if (line.empty()) continue;

        if (!line.empty() && line.front() == '[') {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string raw_val = trim(line.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (key == "name") out.name = parseStringValue(raw_val);
        else if (key == "sprite_tileset") out.sprite_tileset = parseStringValue(raw_val);
        else if (key == "sprite_name") out.sprite_name = parseStringValue(raw_val);
        else if (key == "stackable") {
            bool b = out.stackable;
            if (parseBool(raw_val, b)) out.stackable = b;
        }
    }

    return true;
}

ItemDef fallbackDefFromId(const std::string& id) {
    ItemDef d;
    d.id = id;
    d.name = id;
    d.sprite_tileset = "materials2.tsx";
    d.sprite_name = id;
    return d;
}

} // namespace

std::vector<ItemDef> loadItemDefs(const std::string& dir_path) {
    std::vector<ItemDef> defs;
    const fs::path dir(dir_path);
    if (!fs::exists(dir) || !fs::is_directory(dir)) return defs;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".toml") continue;

        ItemDef d = fallbackDefFromId(entry.path().stem().string());
        if (!parseTomlSubsetFile(entry.path(), d)) {
            std::cerr << "[server] failed to parse item def: " << entry.path().string() << "\n";
            continue;
        }

        if (d.name.empty()) d.name = d.id;
        if (d.sprite_tileset.empty()) d.sprite_tileset = "materials2.tsx";
        if (d.sprite_name.empty()) d.sprite_name = d.id;

        defs.push_back(std::move(d));
    }

    std::sort(defs.begin(), defs.end(), [](const ItemDef& a, const ItemDef& b) {
        return a.id < b.id;
    });
    return defs;
}
