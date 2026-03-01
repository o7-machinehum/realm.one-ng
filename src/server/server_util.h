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

// Sends a pre-serialized packet to a single peer on the given ENet channel.
void sendPacketToPeer(ENetPeer* peer, uint8_t channel,
                      const std::vector<uint8_t>& wire, bool reliable = true);
// Prints a peer's IP address to stdout.
void logPeerAddress(const ENetAddress& addr);
// Serializes and sends the room data to a peer.
void sendRoomToPeer(ENetPeer* peer, const World& world, const std::string& room_name);

// ---- Movement ----

// Clamps a movement delta to -1, 0, or +1.
[[nodiscard]] constexpr int clampToSingleStep(int d) {
    if (d >  1) return  1;
    if (d < -1) return -1;
    return d;
}

// Returns -1, 0, or +1 depending on the sign of v.
[[nodiscard]] constexpr int signum(int v) {
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
}

// ---- Corpse ID helpers ----

// Builds a corpse item ID string from a monster definition ID.
[[nodiscard]] std::string makeCorpseItemId(const std::string& monster_id);

// ---- Equipment ----

// Builds a list of equipment messages from the player's equipment map and inventory.
[[nodiscard]] std::vector<EquippedItemMsg> buildEquipmentMsgList(
    const std::unordered_map<std::string, int>& eq,
    const std::vector<std::string>& inventory);

// Normalizes a raw equipment type string to its canonical form (e.g. "weapon" -> "Weapon").
[[nodiscard]] std::string canonicalEquipType(const std::string& raw);

// ---- Progression ----
struct LevelInfo {
    int level = 1;
    int xp_into_level = 0;
    int xp_to_next = 100;
};

// Returns the XP required to advance from the given level.
[[nodiscard]] int computeExpForLevel(const ProgressionSettings& p, int level);
// Converts a total XP value into a level, XP-into-level, and XP-to-next.
[[nodiscard]] LevelInfo computeLevelFromXp(const ProgressionSettings& p, int xp_total);

// ---- Equipment stat bonuses ----

// Sums a specific stat bonus (via member pointer) across all equipped items.
[[nodiscard]] int computeEquippedStatBonus(
    const PersistedPlayer& p,
    const std::unordered_map<std::string, const ItemDef*>& item_defs_by_id,
    int ItemDef::*member_ptr);

// ---- Derived player combat stats ----

// Computes effective offensive power from melee skill + equipment.
[[nodiscard]] int computePlayerOffence(const PlayerRuntime& p, const ServerState& state);
// Computes effective defence from shielding skill.
[[nodiscard]] int computePlayerDefence(const PlayerRuntime& p, const ServerState& state);
// Computes effective armor value from equipment defense bonuses.
[[nodiscard]] int computePlayerArmor(const PlayerRuntime& p, const ServerState& state);
// Computes effective evasion from evasion skill + equipment.
[[nodiscard]] int computePlayerEvasion(const PlayerRuntime& p, const ServerState& state);
// Recalculates max HP from the player's level and adjusts current HP accordingly.
void updatePlayerVitalsFromLevel(PlayerRuntime& p, const ServerState& state);

// ---- Combat ----

// Returns true with the given percent probability (clamped 0-95).
[[nodiscard]] bool rollPercentChance(int percent);
// Returns true with the given float probability (clamped 0.0-1.0).
[[nodiscard]] bool rollFloatChance(float chance);
// Returns the maximum HP for a given combat level.
[[nodiscard]] constexpr int computeMaxHpForLevel(int level) {
    const int l = (level < 1) ? 1 : level;
    return 100 + (l - 1) * 15;
}

// Sets a combat outcome (miss/block/none) on a player and bumps the sequence counter.
void applyCombatOutcome(PlayerRuntime& p, CombatOutcome outcome);
// Sets a combat outcome (miss/block/none) on a monster and bumps the sequence counter.
void applyCombatOutcome(MonsterRuntime& m, CombatOutcome outcome);
// Records a hit on a player with the given damage value.
void applyCombatHit(PlayerRuntime& p, int value);
// Records a hit on a monster with the given damage value.
void applyCombatHit(MonsterRuntime& m, int value);

// ---- Inventory management ----

// Removes equipment references that point outside the inventory bounds.
void pruneEquipmentForInventory(PlayerRuntime& p);
// Adjusts equipment indices after an inventory slot is erased.
void onInventoryErase(PlayerRuntime& p, int erased_index);
// Adjusts equipment indices after two inventory slots are swapped.
void onInventorySwap(PlayerRuntime& p, int a, int b);

// ---- Authentication ----

// Verifies a login message's signature and authenticates against the database.
[[nodiscard]] bool authenticateLoginRequest(const LoginMsg& m,
                                            AuthDb& auth_db,
                                            PersistedPlayer& persisted,
                                            std::string& message);

// ---- State snapshot & broadcast ----

// Constructs a full game state snapshot for a single player.
[[nodiscard]] GameStateMsg buildGameStateForPlayer(const PlayerRuntime& self,
                                                   const ServerState& state,
                                                   const std::string& event_text);
// Sends a game state update to every authenticated player.
void broadcastGameState(ServerState& state, const std::string& event_text);
// Saves a player's persistent data to the auth database.
void persistPlayer(const PlayerRuntime& p, AuthDb& auth_db);
