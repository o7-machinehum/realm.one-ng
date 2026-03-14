#pragma once

#include "item_defs.h"

#include <optional>
#include <string>
#include <vector>

namespace client {

struct SlotDef {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    std::optional<ItemType> type_constraint; // nullopt = any item
};

struct ContainerDef {
    std::string texture_path; // relative to game/assets/art/
    std::vector<SlotDef> slots;
    int grid_cols = 0; // populated when parsed from a [grid] section
};

ContainerDef loadContainerDef(const std::string& toml_path);

} // namespace client
