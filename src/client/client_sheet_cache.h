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

std::string normalizeKey(std::string s);
std::string parseCorpseMonsterId(const std::string& raw);

std::vector<ItemUiDef> loadClientItemDefs(const std::string& dir_path);

void updateMonsterSheetCache(const GameStateMsg& game_state,
                             std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

void updateItemSheetCacheFromGroundItems(const GameStateMsg& game_state,
                                         std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

void updateItemSheetCacheFromInventory(const GameStateMsg& game_state,
                                       const std::unordered_map<std::string, ItemUiDef>& item_defs_by_key,
                                       const std::unordered_map<std::string, MonsterDef>& monster_defs_by_id,
                                       std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

void unloadSheetCache(std::unordered_map<std::string, SpriteSheetCacheEntry>& cache);

} // namespace client
