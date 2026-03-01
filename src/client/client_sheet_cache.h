#pragma once

#include "msg.h"
#include "monster_defs.h"
#include "sprites.h"
#include "raylib.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace client {

struct SpriteSheetCacheEntry {
    Sprites sprites;
    Texture2D tex{};
    bool ready = false;
    Sprites::SizeOverrideMap size_overrides;
};

struct ItemUiDef {
    std::string id;
    std::string name;
    std::string sprite_tileset;
    std::string equip_type;
};

// Produces a lowercase, trimmed key for id lookups.
std::string normalizeKey(std::string s);

// Parses item UI metadata from TOML files in `dir_path`.
std::vector<ItemUiDef> loadClientItemDefs(const std::string& dir_path);

// Refreshes monster sheet cache entries required by current game state.
void updateMonsterSheetCache(const GameStateMsg& game_state,
                             std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

// Refreshes NPC sheet cache entries required by current game state.
void updateNpcSheetCache(const GameStateMsg& game_state,
                         std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

// Refreshes ground-item sheet cache entries required by current game state.
void updateItemSheetCacheFromGroundItems(const GameStateMsg& game_state,
                                         std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

// Refreshes cache entries needed to render inventory items/corpses.
void updateItemSheetCacheFromInventory(const GameStateMsg& game_state,
                                       const std::unordered_map<std::string, ItemUiDef>& item_defs_by_key,
                                       const std::unordered_map<std::string, MonsterDef>& monster_defs_by_id,
                                       std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

// Unloads textures held by a sheet cache map.
void unloadSheetCache(std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

} // namespace client
