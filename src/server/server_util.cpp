#include "server_util.h"

#include "auth_crypto.h"
#include "string_util.h"
#include "world.h"

#include <algorithm>
#include <iostream>
#include <random>

namespace {

std::mt19937& serverRng() {
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

constexpr const char* kCorpsePrefix = "corpse:";

} // namespace

// ---- Networking ----

void sendPacketToPeer(ENetPeer* peer, uint8_t channel,
                      const std::vector<uint8_t>& wire, bool reliable) {
    ENetPacket* pkt = enet_packet_create(
        wire.data(), wire.size(),
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, channel, pkt);
}

void logPeerAddress(const ENetAddress& a) {
    char ip[64]{};
    enet_address_get_host_ip(&a, ip, sizeof(ip));
    std::cout << ip;
}

void sendRoomToPeer(ENetPeer* peer, const World& world, const std::string& room_name) {
    const Room* room = world.getRoom(room_name);
    if (!room) room = world.defaultRoom();
    if (room) sendPacketToPeer(peer, 0, pack(MsgType::Room, *room));
}

// ---- Corpse ID helpers ----

std::string makeCorpseItemId(const std::string& monster_id) {
    return std::string(kCorpsePrefix) + normalizeId(monster_id);
}

std::string parseCorpseMonsterId(const std::string& item_id) {
    const std::string n = normalizeId(item_id);
    const std::string prefix = kCorpsePrefix;
    if (n.rfind(prefix, 0) != 0) return {};
    return n.substr(prefix.size());
}

// ---- Equipment ----

std::vector<EquippedItemMsg> buildEquipmentMsgList(
    const std::unordered_map<std::string, int>& eq,
    const std::vector<std::string>& inventory) {
    std::vector<EquippedItemMsg> out;
    out.reserve(eq.size());
    for (const auto& [t, idx] : eq) {
        if (t.empty() || idx < 0 || idx >= static_cast<int>(inventory.size())) continue;
        out.push_back(EquippedItemMsg{t, idx, inventory[static_cast<size_t>(idx)]});
    }
    return out;
}

std::string canonicalEquipType(const std::string& raw) {
    const std::string n = normalizeId(raw);
    if (n == "weapon") return "Weapon";
    if (n == "armor")  return "Armor";
    if (n == "shield") return "Shield";
    if (n == "legs")   return "Legs";
    if (n == "boots")  return "Boots";
    if (n == "helmet") return "Helmet";
    return {};
}

// ---- Progression ----

int computeExpForLevel(const ProgressionSettings& p, int level) {
    const int l = std::max(1, level);
    const int need = p.exp_per_level_a * l * l + p.exp_per_level_b * l + p.exp_per_level_c;
    return std::max(1, need);
}

LevelInfo computeLevelFromXp(const ProgressionSettings& p, int xp_total) {
    LevelInfo info{};
    int remaining = std::max(0, xp_total);
    int level = 1;
    while (level < 10000) {
        const int need = computeExpForLevel(p, level);
        if (remaining < need) {
            info.level = level;
            info.xp_into_level = remaining;
            info.xp_to_next = need;
            return info;
        }
        remaining -= need;
        level += 1;
    }
    info.level = level;
    info.xp_into_level = 0;
    info.xp_to_next = computeExpForLevel(p, level);
    return info;
}

// ---- Equipment stat bonuses ----

int computeEquippedStatBonus(
    const PersistedPlayer& p,
    const std::unordered_map<std::string, const ItemDef*>& item_defs_by_id,
    int ItemDef::*member_ptr) {
    auto findDef = [&](const std::string& raw) -> const ItemDef* {
        auto it = item_defs_by_id.find(normalizeId(raw));
        if (it != item_defs_by_id.end() && it->second) return it->second;
        return nullptr;
    };
    int sum = 0;
    for (const auto& [_, inv_idx] : p.equipment_by_type) {
        if (inv_idx < 0 || inv_idx >= static_cast<int>(p.inventory.size())) continue;
        const ItemDef* def = findDef(p.inventory[static_cast<size_t>(inv_idx)]);
        if (!def) continue;
        sum += def->*member_ptr;
    }
    return sum;
}

// ---- Derived player combat stats ----

int computePlayerOffence(const PlayerRuntime& p, const ServerState& state) {
    const int melee = computeLevelFromXp(state.settings.progression, p.data.melee_xp).level;
    const int equip = computeEquippedStatBonus(p.data, state.item_defs_by_id, &ItemDef::attack);
    return std::max(1, melee + equip);
}

int computePlayerDefence(const PlayerRuntime& p, const ServerState& state) {
    const int skill = computeLevelFromXp(state.settings.progression, p.data.shielding_xp).level;
    return std::max(1, skill);
}

int computePlayerArmor(const PlayerRuntime& p, const ServerState& state) {
    const int equip = computeEquippedStatBonus(p.data, state.item_defs_by_id, &ItemDef::defense);
    return std::max(0, equip);
}

int computePlayerEvasion(const PlayerRuntime& p, const ServerState& state) {
    const int skill = computeLevelFromXp(state.settings.progression, p.data.evasion_xp).level;
    const int equip = computeEquippedStatBonus(p.data, state.item_defs_by_id, &ItemDef::evasion);
    return std::max(1, skill + equip);
}

void updatePlayerVitalsFromLevel(PlayerRuntime& p, const ServerState& state) {
    const int lvl = computeLevelFromXp(state.settings.progression, p.data.exp).level;
    const int desired_max_hp = computeMaxHpForLevel(lvl);
    if (p.max_hp != desired_max_hp) {
        const int delta = desired_max_hp - p.max_hp;
        p.max_hp = desired_max_hp;
        if (delta > 0) p.hp += delta;
    }
    p.hp = std::clamp(p.hp, 0, p.max_hp);
}

// ---- Combat ----

bool rollPercentChance(int percent) {
    const int n = std::clamp(percent, 0, 95);
    std::uniform_int_distribution<int> dist(0, 99);
    return dist(serverRng()) < n;
}

bool rollFloatChance(float chance) {
    const float clamped = std::clamp(chance, 0.0f, 1.0f);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(serverRng()) <= clamped;
}

void applyCombatOutcome(PlayerRuntime& p, CombatOutcome outcome) {
    p.combat_outcome = outcome;
    if (outcome != CombatOutcome::Hit) p.combat_value = 0;
    if (outcome != CombatOutcome::None) p.combat_outcome_seq += 1;
}

void applyCombatOutcome(MonsterRuntime& m, CombatOutcome outcome) {
    m.combat_outcome = outcome;
    if (outcome != CombatOutcome::Hit) m.combat_value = 0;
    if (outcome != CombatOutcome::None) m.combat_outcome_seq += 1;
}

void applyCombatHit(PlayerRuntime& p, int value) {
    p.combat_outcome = CombatOutcome::Hit;
    p.combat_value = std::max(0, value);
    p.combat_outcome_seq += 1;
}

void applyCombatHit(MonsterRuntime& m, int value) {
    m.combat_outcome = CombatOutcome::Hit;
    m.combat_value = std::max(0, value);
    m.combat_outcome_seq += 1;
}

// ---- Inventory management ----

void pruneEquipmentForInventory(PlayerRuntime& p) {
    for (auto it = p.data.equipment_by_type.begin(); it != p.data.equipment_by_type.end();) {
        if (it->second < 0 || it->second >= static_cast<int>(p.data.inventory.size())) {
            it = p.data.equipment_by_type.erase(it);
        } else {
            ++it;
        }
    }
}

void onInventoryErase(PlayerRuntime& p, int erased_index) {
    for (auto it = p.data.equipment_by_type.begin(); it != p.data.equipment_by_type.end();) {
        if (it->second == erased_index) {
            it = p.data.equipment_by_type.erase(it);
        } else {
            if (it->second > erased_index) it->second -= 1;
            ++it;
        }
    }
}

void onInventorySwap(PlayerRuntime& p, int a, int b) {
    for (auto& [_, idx] : p.data.equipment_by_type) {
        if (idx == a) idx = b;
        else if (idx == b) idx = a;
    }
}

// ---- Authentication ----

bool authenticateLoginRequest(const LoginMsg& m,
                              AuthDb& auth_db,
                              PersistedPlayer& persisted,
                              std::string& message) {
    if (m.user.empty() || m.public_key_hex.empty() || m.signature_hex.empty()) {
        message = "missing login fields";
        return false;
    }
    const std::string payload = makeAuthPayload(m.user, m.public_key_hex, m.create_account);
    if (!verifyEd25519Hex(m.public_key_hex, payload, m.signature_hex)) {
        message = "invalid signature";
        return false;
    }
    return auth_db.loginWithPublicKey(
        m.user, m.public_key_hex, m.create_account, persisted, message);
}

// ---- State snapshot & broadcast ----

GameStateMsg buildGameStateForPlayer(const PlayerRuntime& self,
                                     const ServerState& state,
                                     const std::string& event_text) {
    GameStateMsg gs;
    gs.your_user = self.data.username;
    gs.your_room = self.data.room;
    gs.your_x = self.data.x;
    gs.your_y = self.data.y;
    gs.your_exp = self.data.exp;
    gs.your_hp = self.hp;
    gs.your_max_hp = self.max_hp;
    gs.your_mana = self.mana;
    gs.your_max_mana = self.max_mana;

    const LevelInfo lvl      = computeLevelFromXp(state.settings.progression, self.data.exp);
    const LevelInfo melee    = computeLevelFromXp(state.settings.progression, self.data.melee_xp);
    const LevelInfo distance = computeLevelFromXp(state.settings.progression, self.data.distance_xp);
    const LevelInfo magic    = computeLevelFromXp(state.settings.progression, self.data.magic_xp);
    const LevelInfo shielding = computeLevelFromXp(state.settings.progression, self.data.shielding_xp);
    const LevelInfo evasion  = computeLevelFromXp(state.settings.progression, self.data.evasion_xp);

    const int equip_attack  = computeEquippedStatBonus(self.data, state.item_defs_by_id, &ItemDef::attack);
    const int equip_defense = computeEquippedStatBonus(self.data, state.item_defs_by_id, &ItemDef::defense);
    const int equip_evasion = computeEquippedStatBonus(self.data, state.item_defs_by_id, &ItemDef::evasion);

    const int trait_hit   = melee.level;
    const int trait_block = shielding.level;
    const int trait_evade = evasion.level;
    const int trait_armor = std::max(0, equip_defense);
    const int trait_attack = std::max(1, trait_hit + equip_attack);

    gs.your_level = lvl.level;
    gs.your_exp = lvl.xp_into_level;
    gs.your_exp_to_next_level = lvl.xp_to_next;

    gs.skill_melee_level     = melee.level;
    gs.skill_melee_xp        = melee.xp_into_level;
    gs.skill_melee_xp_to_next = melee.xp_to_next;
    gs.skill_distance_level  = distance.level;
    gs.skill_distance_xp     = distance.xp_into_level;
    gs.skill_distance_xp_to_next = distance.xp_to_next;
    gs.skill_magic_level     = magic.level;
    gs.skill_magic_xp        = magic.xp_into_level;
    gs.skill_magic_xp_to_next = magic.xp_to_next;
    gs.skill_shielding_level = shielding.level;
    gs.skill_shielding_xp    = shielding.xp_into_level;
    gs.skill_shielding_xp_to_next = shielding.xp_to_next;
    gs.skill_evasion_level   = evasion.level;
    gs.skill_evasion_xp      = evasion.xp_into_level;
    gs.skill_evasion_xp_to_next = evasion.xp_to_next;

    gs.derived_defence  = std::max(1, trait_block + trait_armor);
    gs.derived_offence  = std::max(1, trait_attack);
    gs.derived_evasion  = std::max(1, trait_evade + equip_evasion);
    gs.trait_attack     = trait_attack;
    gs.trait_shielding  = std::max(1, trait_block);
    gs.trait_evasion    = std::max(1, trait_evade + equip_evasion);
    gs.trait_armor      = trait_armor;

    gs.your_equipment = buildEquipmentMsgList(self.data.equipment_by_type, self.data.inventory);
    gs.attack_target_monster_id = self.attack_target_monster_id;
    gs.inventory = self.data.inventory;
    gs.event_text = event_text;

    // ---- Populate room-local players ----
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != self.data.room) continue;

        PlayerStateMsg ps;
        ps.user     = p.data.username;
        ps.room     = p.data.room;
        ps.x        = p.data.x;
        ps.y        = p.data.y;
        ps.exp      = p.data.exp;
        ps.hp       = p.hp;
        ps.max_hp   = p.max_hp;
        ps.mana     = p.mana;
        ps.max_mana = p.max_mana;
        ps.facing   = facingToInt(p.facing);
        ps.attack_anim_seq    = p.attack_anim_seq;
        ps.combat_outcome     = combatOutcomeToInt(p.combat_outcome);
        ps.combat_outcome_seq = p.combat_outcome_seq;
        ps.combat_value       = p.combat_value;
        ps.equipment = buildEquipmentMsgList(p.data.equipment_by_type, p.data.inventory);
        gs.players.push_back(std::move(ps));
    }

    // ---- Populate room-local monsters ----
    for (const auto& m : state.monsters) {
        if (m.room != self.data.room) continue;
        gs.monsters.push_back(MonsterStateMsg{
            m.id, m.name, m.sprite_tileset, m.sprite_name,
            m.size_w, m.size_h, m.room, m.x, m.y,
            m.hp, m.max_hp,
            facingToInt(m.facing), m.attack_anim_seq,
            combatOutcomeToInt(m.combat_outcome), m.combat_outcome_seq, m.combat_value
        });
    }

    // ---- Populate room-local NPCs ----
    for (const auto& n : state.npcs) {
        if (n.room != self.data.room) continue;
        gs.npcs.push_back(NpcStateMsg{
            n.id, n.name, n.sprite_tileset, n.sprite_name,
            n.size_w, n.size_h, n.room, n.x, n.y,
            facingToInt(n.facing)
        });
    }

    // ---- Populate room-local ground items ----
    for (const auto& i : state.items) {
        if (i.room != self.data.room) continue;
        gs.items.push_back(GroundItemStateMsg{
            i.id, i.name, i.sprite_tileset, i.sprite_name,
            i.sprite_w_tiles, i.sprite_h_tiles, i.sprite_clip,
            i.room, i.x, i.y
        });
    }

    return gs;
}

void broadcastGameState(ServerState& state, const std::string& event_text) {
    for (const auto& [peer, p] : state.players) {
        if (!p.authenticated) continue;
        auto gs = buildGameStateForPlayer(p, state, event_text);
        sendPacketToPeer(peer, 0, pack(MsgType::GameState, gs));
    }
    enet_host_flush(state.host);
}

void persistPlayer(const PlayerRuntime& p, AuthDb& auth_db) {
    if (p.authenticated) auth_db.savePlayer(p.data);
}
