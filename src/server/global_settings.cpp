#include "global_settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

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

} // namespace

GlobalSettings loadGlobalSettings(const std::string& path) {
    GlobalSettings out{};
    std::ifstream in(path);
    if (!in) return out;

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(stripComment(line));
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            std::transform(section.begin(), section.end(), section.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        int n = 0;
        if (section.empty() || section == "progression") {
            if (key == "exp_per_level_a") {
                if (parseInt(value, n)) out.progression.exp_per_level_a = n;
            } else if (key == "exp_per_level_b") {
                if (parseInt(value, n)) out.progression.exp_per_level_b = n;
            } else if (key == "exp_per_level_c") {
                if (parseInt(value, n)) out.progression.exp_per_level_c = n;
            }
        } else if (section == "gameplay") {
            if (key == "monster_respawn_ms") {
                if (parseInt(value, n)) out.gameplay.monster_respawn_ms = std::max(0, n);
            }
        }
    }

    return out;
}
