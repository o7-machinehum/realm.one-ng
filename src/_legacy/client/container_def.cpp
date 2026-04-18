#include "container_def.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

namespace client {

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

} // namespace

ContainerDef loadContainerDef(const std::string& toml_path) {
    ContainerDef def;
    std::ifstream in(toml_path);
    if (!in) {
        std::cerr << "[container_def] Failed to open " << toml_path << "\n";
        return def;
    }

    enum Section { None, Slots, Grid };
    Section section = None;

    SlotDef current_slot{};
    bool has_current_slot = false;

    // Grid parameters
    float grid_start_x = 0, grid_start_y = 0;
    float grid_slot_w = 32, grid_slot_h = 32;
    float grid_gap_x = 4, grid_gap_y = 4;
    int grid_cols = 1, grid_rows = 1;
    bool has_grid = false;

    auto flushSlot = [&]() {
        if (has_current_slot) {
            def.slots.push_back(current_slot);
            current_slot = SlotDef{};
            has_current_slot = false;
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        line = trim(stripComment(line));
        if (line.empty()) continue;

        // Section headers
        if (line == "[[slots]]") {
            flushSlot();
            section = Slots;
            has_current_slot = true;
            continue;
        }
        if (line == "[grid]") {
            flushSlot();
            section = Grid;
            has_grid = true;
            continue;
        }
        if (line.front() == '[') {
            flushSlot();
            section = None;
            continue;
        }

        // Key-value pairs
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (section == None) {
            if (key == "texture") def.texture_path = parseStringValue(value);
        } else if (section == Slots) {
            if (key == "x") parseFloat(value, current_slot.x);
            else if (key == "y") parseFloat(value, current_slot.y);
            else if (key == "w") parseFloat(value, current_slot.w);
            else if (key == "h") parseFloat(value, current_slot.h);
            else if (key == "item_type") {
                current_slot.type_constraint = stringToItemType(parseStringValue(value));
            }
        } else if (section == Grid) {
            if (key == "start_x") parseFloat(value, grid_start_x);
            else if (key == "start_y") parseFloat(value, grid_start_y);
            else if (key == "slot_w") parseFloat(value, grid_slot_w);
            else if (key == "slot_h") parseFloat(value, grid_slot_h);
            else if (key == "gap_x") parseFloat(value, grid_gap_x);
            else if (key == "gap_y") parseFloat(value, grid_gap_y);
            else if (key == "cols") parseInt(value, grid_cols);
            else if (key == "rows") parseInt(value, grid_rows);
        }
    }

    flushSlot();

    // Generate slots from grid
    if (has_grid) {
        def.grid_cols = grid_cols;
        for (int r = 0; r < grid_rows; ++r) {
            for (int c = 0; c < grid_cols; ++c) {
                SlotDef s;
                s.x = grid_start_x + c * (grid_slot_w + grid_gap_x);
                s.y = grid_start_y + r * (grid_slot_h + grid_gap_y);
                s.w = grid_slot_w;
                s.h = grid_slot_h;
                def.slots.push_back(s);
            }
        }
    }

    return def;
}

} // namespace client
