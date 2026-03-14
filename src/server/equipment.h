#pragma once

#include "item_instance.h"
#include "server_state.h"
#include "string_util.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

int computeEquippedStatBonus(
    const PersistedPlayer& p,
    const std::unordered_map<int64_t, ItemInstance>& instances,
    const std::unordered_map<std::string, const ItemDef*>& item_defs_by_id,
    int ItemDef::*member_ptr);
