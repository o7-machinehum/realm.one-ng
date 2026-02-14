#include "ui_settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
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

} // namespace

UiSettings loadUiSettings(const std::string& path) {
    UiSettings out{};
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

        // Support both top-level keys and [ui] section keys.
        if (!section.empty() && section != "ui") continue;

        float f = 0.0f;
        if (key == "speech_bubble_alpha") {
            if (parseFloat(value, f)) out.speech_bubble_alpha = std::max(0.0f, std::min(1.0f, f));
        } else if (key == "player_name_text_size") {
            if (parseFloat(value, f)) out.player_name_text_size = std::max(6.0f, f);
        } else if (key == "monster_name_text_size") {
            if (parseFloat(value, f)) out.monster_name_text_size = std::max(6.0f, f);
        } else if (key == "npc_name_text_size") {
            if (parseFloat(value, f)) out.npc_name_text_size = std::max(6.0f, f);
        } else if (key == "speech_text_size") {
            if (parseFloat(value, f)) out.speech_text_size = std::max(8.0f, f);
        }
    }

    return out;
}

} // namespace client
