// Consolidated string helpers used across client and server code.
// Single source of truth for whitespace trimming, case folding, and ID normalization.
#pragma once

#include <string>

// Strips leading and trailing whitespace.
[[nodiscard]] std::string trimWhitespace(std::string s);

// Folds every ASCII letter to lowercase.
[[nodiscard]] std::string toLowerAscii(std::string s);

// Trims then lowercases -- the canonical normalization for entity ID lookups.
[[nodiscard]] std::string normalizeId(std::string s);

// Extracts the monster definition ID from a corpse item ID (e.g. "corpse:rat" -> "rat").
// Returns an empty string if the item is not a corpse.
[[nodiscard]] std::string parseCorpseMonsterId(const std::string& item_id);
