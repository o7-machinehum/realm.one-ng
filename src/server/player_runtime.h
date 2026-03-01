// Runtime state for a single connected player on the server.
#pragma once

#include "auth_db.h"
#include "combat_types.h"

#include "enet_compat.h"

#include <cstdint>

// Transient runtime state for a single connected player.
// Persisted fields live in PersistedPlayer (data member).
struct PlayerRuntime {
    // ---- Connection ----
    ENetPeer* peer = nullptr;        // Network peer handle.
    bool authenticated = false;      // True after successful login.
    PersistedPlayer data;            // Database-backed player data.

    // ---- Vitals ----
    int hp = 100;                    // Current hit points.
    int max_hp = 100;                // Maximum hit points (level-derived).
    int mana = 60;                   // Current mana.
    int max_mana = 60;               // Maximum mana.

    // ---- Combat ----
    Facing facing = Facing::South;   // Current facing direction.
    uint32_t attack_anim_seq = 0;    // Incremented each attack for animation sync.
    CombatOutcome combat_outcome = CombatOutcome::None;
    uint32_t combat_outcome_seq = 0; // Incremented on each combat outcome.
    int combat_value = 0;            // Damage dealt in last hit outcome.
    int attack_target_monster_id = -1; // Monster being auto-attacked, or -1.
};
