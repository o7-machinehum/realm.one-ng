// Runtime state for a single connected player on the server.
#pragma once

#include "auth_db.h"
#include "combat_types.h"

#include "enet_compat.h"

#include <cstdint>

struct PlayerRuntime {
    ENetPeer* peer = nullptr;
    bool authenticated = false;
    PersistedPlayer data;

    // ---- Vitals ----
    int hp = 100;
    int max_hp = 100;
    int mana = 60;
    int max_mana = 60;

    // ---- Combat ----
    Facing facing = Facing::South;
    uint32_t attack_anim_seq = 0;
    CombatOutcome combat_outcome = CombatOutcome::None;
    uint32_t combat_outcome_seq = 0;
    int combat_value = 0;
    int attack_target_monster_id = -1;
};
