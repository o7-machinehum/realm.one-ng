#include "equipment.h"

// ---- Equipment stat bonuses ----
int computeEquippedStatBonus(
    const PersistedPlayer& p,
    const std::unordered_map<int64_t, ItemInstance>& instances,
    const std::unordered_map<std::string, const ItemDef*>& item_defs_by_id,
    int ItemDef::*member_ptr) {
    int sum = 0;
    for (const auto& [_, iid] : p.equipment_by_type) {
        if (iid <= 0) continue;
        auto inst_it = instances.find(iid);
        if (inst_it == instances.end()) continue;
        auto def_it = item_defs_by_id.find(normalizeId(inst_it->second.def_id));
        if (def_it == item_defs_by_id.end() || !def_it->second) continue;
        sum += def_it->second->*member_ptr;
    }
    return sum;
}
