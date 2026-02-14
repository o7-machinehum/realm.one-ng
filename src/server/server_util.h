// Pure utility functions extracted from the net_server anonymous namespace.
// Networking helpers, combat math, progression formulas, and state building.
#pragma once

#include "combat_types.h"
#include "server_state.h"
#include "msg.h"
#include "envelope.h"

#include <string>
#include <vector>

// ---- Networking ----
void sendPacketToPeer(ENetPeer* peer, uint8_t channel,
                      const std::vector<uint8_t>& wire, bool reliable = true);
void logPeerAddress(const ENetAddress& addr);
void sendRoomToPeer(ENetPeer* peer, const World& world, const std::string& room_name);

// ---- Movement ----
[[nodiscard]] constexpr int clampToSingleStep(int d) {
    if (d >  1) return  1;
    if (d < -1) return -1;
    return d;
}

[[nodiscard]] constexpr int signum(int v) {
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
}

// ---- Corpse ID helpers ----
[[nodiscard]] std::string makeCorpseItemId(const std::string& monster_id);
[[nodiscard]] std::string parseCorpseMonsterId(const std::string& item_id);

// ---- Equipment ----
[[nodiscard]] std::vector<EquippedItemMsg> buildEquipmentMsgList(
    const std::unordered_map<std::string, int>& eq,
    const std::vector<std::string>& inventory);

[[nodiscard]] std::string canonicalEquipType(const std::string& raw);

// ---- Progression ----
struct LevelInfo {
    int level = 1;
    int xp_into_level = 0;
    int xp_to_next = 100;
};

[[nodiscard]] int computeExpForLevel(const ProgressionSettings& p, int level);
[[nodiscard]] LevelInfo computeLevelFromXp(const ProgressionSettings& p, int xp_total);

// ---- Equipment stat bonuses ----
[[nodiscard]] int computeEquippedStatBonus(
    const PersistedPlayer& p,
    const std::unordered_map<std::string, const ItemDef*>& item_defs_by_id,
    int ItemDef::*member_ptr);

// ---- Derived player combat stats ----
[[nodiscard]] int computePlayerOffence(const PlayerRuntime& p, const ServerState& state);
[[nodiscard]] int computePlayerDefence(const PlayerRuntime& p, const ServerState& state);
[[nodiscard]] int computePlayerArmor(const PlayerRuntime& p, const ServerState& state);
[[nodiscard]] int computePlayerEvasion(const PlayerRuntime& p, const ServerState& state);
void updatePlayerVitalsFromLevel(PlayerRuntime& p, const ServerState& state);

// ---- Combat ----
[[nodiscard]] bool rollPercentChance(int percent);
[[nodiscard]] bool rollFloatChance(float chance);
[[nodiscard]] constexpr int computeMaxHpForLevel(int level) {
    const int l = (level < 1) ? 1 : level;
    return 100 + (l - 1) * 15;
}

void applyCombatOutcome(PlayerRuntime& p, CombatOutcome outcome);
void applyCombatOutcome(MonsterRuntime& m, CombatOutcome outcome);
void applyCombatHit(PlayerRuntime& p, int value);
void applyCombatHit(MonsterRuntime& m, int value);

// ---- Inventory management ----
void pruneEquipmentForInventory(PlayerRuntime& p);
void onInventoryErase(PlayerRuntime& p, int erased_index);
void onInventorySwap(PlayerRuntime& p, int a, int b);

// ---- Authentication ----
[[nodiscard]] bool authenticateLoginRequest(const LoginMsg& m,
                                            AuthDb& auth_db,
                                            PersistedPlayer& persisted,
                                            std::string& message);

// ---- State snapshot & broadcast ----
[[nodiscard]] GameStateMsg buildGameStateForPlayer(const PlayerRuntime& self,
                                                   const ServerState& state,
                                                   const std::string& event_text);
void broadcastGameState(ServerState& state, const std::string& event_text);
void persistPlayer(const PlayerRuntime& p, AuthDb& auth_db);
