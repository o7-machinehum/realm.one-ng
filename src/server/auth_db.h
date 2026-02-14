#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct sqlite3;

struct PersistedPlayer {
    std::string username;
    std::string room;
    int exp = 0;
    int x = 2;
    int y = 2;
    int melee_xp = 0;
    int distance_xp = 0;
    int magic_xp = 0;
    int shielding_xp = 0;
    int evasion_xp = 0;
    std::vector<std::string> inventory;
    std::unordered_map<std::string, int> equipment_by_type; // equip_type -> inventory slot index
};

class AuthDb {
public:
    explicit AuthDb(const std::string& db_path);
    ~AuthDb();

    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;

    bool loginWithPublicKey(const std::string& username,
                            const std::string& public_key_hex,
                            bool create_account,
                            PersistedPlayer& out_player,
                            std::string& message);
    bool savePlayer(const PersistedPlayer& player);

private:
    sqlite3* db_ = nullptr;
};
