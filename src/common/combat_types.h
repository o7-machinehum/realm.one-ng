// Shared combat and facing types used by both server logic and message building.
#pragma once

// Outcome of a single combat exchange (attack roll).
enum class CombatOutcome : int { None = 0, Hit = 1, Missed = 2, Blocked = 3 };

// Cardinal facing direction for sprite rendering and movement.
enum class Facing : int { North = 0, East = 1, South = 2, West = 3 };

[[nodiscard]] constexpr int facingToInt(Facing f) { return static_cast<int>(f); }

[[nodiscard]] constexpr Facing facingFromInt(int v) {
    switch (v) {
        case 0:  return Facing::North;
        case 1:  return Facing::East;
        case 2:  return Facing::South;
        case 3:  return Facing::West;
        default: return Facing::South;
    }
}

[[nodiscard]] constexpr int combatOutcomeToInt(CombatOutcome o) { return static_cast<int>(o); }

[[nodiscard]] constexpr CombatOutcome combatOutcomeFromInt(int v) {
    switch (v) {
        case 0:  return CombatOutcome::None;
        case 1:  return CombatOutcome::Hit;
        case 2:  return CombatOutcome::Missed;
        case 3:  return CombatOutcome::Blocked;
        default: return CombatOutcome::None;
    }
}

// Determines facing direction from a movement delta.
[[nodiscard]] constexpr Facing facingFromDelta(int dx, int dy,
                                               Facing fallback = Facing::South) {
    if (dx > 0) return Facing::East;
    if (dx < 0) return Facing::West;
    if (dy > 0) return Facing::South;
    if (dy < 0) return Facing::North;
    return fallback;
}
