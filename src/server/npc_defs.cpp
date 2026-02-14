#include "npc_defs.h"

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

std::vector<std::string> splitQuestions(const std::string& raw) {
    std::vector<std::string> out;
    const std::string text = parseStringValue(raw);
    std::string acc;
    for (char c : text) {
        if (c == ',') {
            acc = trim(acc);
            if (!acc.empty()) out.push_back(acc);
            acc.clear();
            continue;
        }
        acc.push_back(c);
    }
    acc = trim(acc);
    if (!acc.empty()) out.push_back(acc);
    return out;
}

bool parseTomlSubsetFile(const fs::path& p, NpcDef& out) {
    std::ifstream in(p);
    if (!in) return false;

    NpcDialogueDef current_dialogue{};
    bool in_dialogue_table = false;
    bool current_dialogue_valid = false;
    auto flush_dialogue = [&]() {
        if (!in_dialogue_table || !current_dialogue_valid) return;
        if (current_dialogue.questions.empty() || current_dialogue.response.empty()) return;
        out.dialogues.push_back(current_dialogue);
    };

    std::string line;
    while (std::getline(in, line)) {
        line = trim(stripComment(line));
        if (line.empty()) continue;

        if (line == "[[dialogue]]") {
            flush_dialogue();
            current_dialogue = NpcDialogueDef{};
            in_dialogue_table = true;
            current_dialogue_valid = true;
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            flush_dialogue();
            in_dialogue_table = false;
            current_dialogue_valid = false;
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string raw_val = trim(line.substr(eq + 1));
        if (key.empty() || raw_val.empty()) continue;

        if (in_dialogue_table) {
            if (key == "question" || key == "questions" || key == "topic") {
                std::vector<std::string> parts = splitQuestions(raw_val);
                current_dialogue.questions.insert(current_dialogue.questions.end(), parts.begin(), parts.end());
            } else if (key == "response" || key == "responce") {
                current_dialogue.response = parseStringValue(raw_val);
            }
            continue;
        }

        if (key == "name") out.name = parseStringValue(raw_val);
        else if (key == "sprite_tileset") out.sprite_tileset = parseStringValue(raw_val);
        else if (key == "npc_size" || key == "monster_size" || key == "size") {
            int w = out.npc_size_w;
            int h = out.npc_size_h;
            if (parseSize(raw_val, w, h)) {
                out.npc_size_w = w;
                out.npc_size_h = h;
            }
        } else if (key == "speed_ms") {
            int v = out.speed_ms;
            if (parseInt(raw_val, v)) out.speed_ms = std::max(1, v);
        } else if (key == "wander_radius" || key == "area_radius") {
            int v = out.wander_radius;
            if (parseInt(raw_val, v)) out.wander_radius = std::max(0, v);
        }
    }

    flush_dialogue();
    return true;
}

NpcDef fallbackDefFromId(const std::string& id) {
    NpcDef d;
    d.id = id;
    d.name = id;
    d.sprite_tileset = "character.tsx";
    return d;
}

} // namespace

std::vector<NpcDef> loadNpcDefs(const std::string& dir_path) {
    std::vector<NpcDef> defs;
    const fs::path dir(dir_path);
    if (!fs::exists(dir) || !fs::is_directory(dir)) return defs;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".toml") continue;

        NpcDef d = fallbackDefFromId(entry.path().stem().string());
        if (!parseTomlSubsetFile(entry.path(), d)) {
            std::cerr << "[server] failed to parse npc def: " << entry.path().string() << "\n";
            continue;
        }

        if (d.name.empty()) d.name = d.id;
        if (d.sprite_tileset.empty()) d.sprite_tileset = "character.tsx";

        defs.push_back(std::move(d));
    }

    std::sort(defs.begin(), defs.end(), [](const NpcDef& a, const NpcDef& b) {
        return a.id < b.id;
    });
    return defs;
}
