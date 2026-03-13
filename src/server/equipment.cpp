#include "equipment.h"

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
