#include "client_sheet_cache.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace client {
namespace {

constexpr const char* kCorpsePrefix = "corpse:";

// Trims surrounding whitespace from parser tokens.
std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

// Reads a TOML string literal or returns the raw token for bare values.
std::string parseTomlString(const std::string& raw) {
    std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

// Reloads a sheet only when sprite-size overrides changed for that key.
bool refreshSheetCacheEntry(SpriteSheetCacheEntry& entry,
                            const std::string& key,
                            const std::pair<int, int>& size,
                            const std::string& tsx_rel_path) {
    bool needs_reload = !entry.ready;
    auto sit = entry.size_overrides.find(key);
    if (sit == entry.size_overrides.end() || sit->second != size) {
        entry.size_overrides[key] = size;
        needs_reload = true;
    }
    if (!needs_reload) return true;

    if (entry.tex.id != 0) {
        UnloadTexture(entry.tex);
        entry.tex = Texture2D{};
    }

    const std::string tsx_path = "game/assets/art/" + tsx_rel_path;
    if (!entry.sprites.loadTSX(tsx_path, entry.size_overrides)) {
        entry.ready = false;
        return false;
    }

    const std::string tex_path = "game/assets/art/" + entry.sprites.image_source();
    entry.tex = LoadTexture(tex_path.c_str());
    entry.ready = (entry.tex.id != 0);
    return entry.ready;
}

} // namespace

// Normalizes ids from file/user data so lookups are case/space insensitive.
std::string normalizeKey(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return trim(std::move(s));
}

// Extracts monster id from corpse inventory entries like "corpse:rat".
std::string parseCorpseMonsterId(const std::string& raw) {
    const std::string n = normalizeKey(raw);
    const std::string prefix = kCorpsePrefix;
    if (n.rfind(prefix, 0) != 0) return {};
    return n.substr(prefix.size());
}

// Loads lightweight item UI metadata from item TOML definitions.
std::vector<ItemUiDef> loadClientItemDefs(const std::string& dir_path) {
    std::vector<ItemUiDef> out;
    namespace fs = std::filesystem;
    fs::path dir(dir_path);
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") continue;
        ItemUiDef def;
        def.id = entry.path().stem().string();
        def.name = def.id;

        std::ifstream in(entry.path());
        if (!in) continue;

        std::string line;
        while (std::getline(in, line)) {
            const auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            line = trim(line);
            if (line.empty() || line.front() == '[') continue;

            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq));
            const std::string value = trim(line.substr(eq + 1));
            if (key == "name") def.name = parseTomlString(value);
            else if (key == "sprite_tileset") def.sprite_tileset = parseTomlString(value);
            else if (key == "equip_type" || key == "item_type" || key == "type" || key == "slot") {
                def.equip_type = parseTomlString(value);
            }
        }

        if (def.sprite_tileset.empty()) def.sprite_tileset = "materials2.tsx";
        out.push_back(std::move(def));
    }
    return out;
}

// Ensures all visible monster sheets are loaded with current runtime sizes.
void updateMonsterSheetCache(const GameStateMsg& game_state,
                             std::unordered_map<std::string, SpriteSheetCacheEntry>& cache) {
    for (const auto& m : game_state.monsters) {
        if (m.sprite_tileset.empty()) continue;
        auto& entry = cache[m.sprite_tileset];
        const std::string size_key = normalizeKey(m.sprite_name.empty() ? m.name : m.sprite_name);
        const std::pair<int, int> size_val{std::max(1, m.sprite_w_tiles), std::max(1, m.sprite_h_tiles)};
        refreshSheetCacheEntry(entry, size_key, size_val, m.sprite_tileset);
    }
}

// Ensures all visible NPC sheets are loaded with current runtime sizes.
void updateNpcSheetCache(const GameStateMsg& game_state,
                         std::unordered_map<std::string, SpriteSheetCacheEntry>& cache) {
    for (const auto& n : game_state.npcs) {
        if (n.sprite_tileset.empty()) continue;
        auto& entry = cache[n.sprite_tileset];
        const std::string size_key = normalizeKey(n.sprite_name.empty() ? n.name : n.sprite_name);
        const std::pair<int, int> size_val{std::max(1, n.sprite_w_tiles), std::max(1, n.sprite_h_tiles)};
        refreshSheetCacheEntry(entry, size_key, size_val, n.sprite_tileset);
    }
}

// Ensures all visible ground-item sheets are loaded with current runtime sizes.
void updateItemSheetCacheFromGroundItems(const GameStateMsg& game_state,
                                         std::unordered_map<std::string, SpriteSheetCacheEntry>& cache) {
    for (const auto& i : game_state.items) {
        if (i.sprite_tileset.empty() || i.sprite_name.empty()) continue;
        auto& entry = cache[i.sprite_tileset];
        const std::string size_key = normalizeKey(i.sprite_name);
        const std::pair<int, int> size_val{std::max(1, i.sprite_w_tiles), std::max(1, i.sprite_h_tiles)};
        refreshSheetCacheEntry(entry, size_key, size_val, i.sprite_tileset);
    }
}

// Preloads sheets needed to draw inventory entries, including monster corpses.
void updateItemSheetCacheFromInventory(const GameStateMsg& game_state,
                                       const std::unordered_map<std::string, ItemUiDef>& item_defs_by_key,
                                       const std::unordered_map<std::string, MonsterDef>& monster_defs_by_id,
                                       std::unordered_map<std::string, SpriteSheetCacheEntry>& cache) {
    for (const auto& inv_item : game_state.inventory) {
        auto dit = item_defs_by_key.find(normalizeKey(inv_item));
        std::string tsx;
        std::string size_key;
        std::pair<int, int> size_val{1, 1};

        if (dit != item_defs_by_key.end()) {
            const auto& def = dit->second;
            tsx = def.sprite_tileset;
            size_key = normalizeKey(def.id);
        } else {
            const std::string corpse_id = parseCorpseMonsterId(inv_item);
            if (corpse_id.empty()) continue;
            auto mit = monster_defs_by_id.find(corpse_id);
            if (mit == monster_defs_by_id.end()) continue;
            tsx = mit->second.sprite_tileset;
            size_key = normalizeKey(mit->second.id);
            size_val = {
                std::max(1, mit->second.monster_size_w),
                std::max(1, mit->second.monster_size_h)
            };
        }

        auto& entry = cache[tsx];
        refreshSheetCacheEntry(entry, size_key, size_val, tsx);
    }
}

// Releases textures owned by a cache map before shutdown.
void unloadSheetCache(std::unordered_map<std::string, SpriteSheetCacheEntry>& cache) {
    for (auto& [_, entry] : cache) {
        if (entry.tex.id != 0) UnloadTexture(entry.tex);
    }
}

} // namespace client
