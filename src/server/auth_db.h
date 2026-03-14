#pragma once

#include "item_defs.h"
#include "tile_pos.h"

#include <map>
#include <string>
#include <vector>

struct sqlite3;

// Data saved to/loaded from the database for each player between sessions.
struct PersistedPlayer {
    std::string username;
    std::string room;                // Qualified room name (e.g. "500_500:0.tmx").
    int exp = 0;
    TilePos pos{2, 2};              // Tile position within the room.
    int melee_xp = 0;
    int distance_xp = 0;
    int magic_xp = 0;
    int shielding_xp = 0;
    int evasion_xp = 0;
    std::vector<std::string> inventory;
    std::map<ItemType, int> equipment_by_type;
};

// SQLite-backed player authentication and persistence.
class AuthDb {
public:
    // Opens (or creates) the SQLite database at db_path, applying schema migrations.
    explicit AuthDb(const std::string& db_path);
    ~AuthDb();

    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;

    // Authenticates or creates a player account using Ed25519 public key.
    // On success, fills out_player with persisted data and returns true.
    bool loginWithPublicKey(const std::string& username,
                            const std::string& public_key_hex,
                            bool create_account,
                            PersistedPlayer& out_player,
                            std::string& message);
    // Writes the player's current state back to the database.
    bool savePlayer(const PersistedPlayer& player);

private:
    sqlite3* db_ = nullptr;
};
