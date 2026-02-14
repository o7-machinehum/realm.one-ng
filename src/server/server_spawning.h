// Entity spawning: creates initial monsters, NPCs, and ground items from room data.
#pragma once

#include "server_state.h"

#include <string>

// Spawns a single ground item by its definition ID at the given position.
void spawnGroundItem(ServerState& state, const std::string& raw_item_id,
                     const std::string& room_name, int x, int y);

// Iterates all rooms in the world and creates initial monsters, NPCs, and items
// from the spawn data authored in Tiled.
void spawnInitialEntities(ServerState& state);
