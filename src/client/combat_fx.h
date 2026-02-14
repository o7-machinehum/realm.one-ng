// Combat effect atlas: loads hit/miss/block frame sequences from the tileset.
#pragma once

#include <utility>
#include <vector>

namespace client {

struct CombatFxAtlas {
    int columns = 40;
    int tile_w = 16;
    int tile_h = 16;
    std::vector<int> hit_frames;
    std::vector<int> wiff_frames;
    std::vector<int> block_frames;
    bool loaded = false;
};

// Returns the lazily-initialized combat FX atlas singleton.
[[nodiscard]] const CombatFxAtlas& combatFxAtlas();

// Sorts sequence pairs by index and extracts the tile IDs in order.
[[nodiscard]] std::vector<int> buildFrameSequence(std::vector<std::pair<int, int>> seq_pairs,
                                                   int fallback_anchor);

} // namespace client
