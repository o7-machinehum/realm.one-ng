#pragma once

#include "combat_types.h"
#include "server_state.h"
#include "string_util.h"

#include <vector>

int computeEquippedStatBonus(
const PersistedPlayer& p,
    const std::unordered_map<std::string, const ItemDef*>& item_defs_by_id,
    int ItemDef::*member_ptr);
