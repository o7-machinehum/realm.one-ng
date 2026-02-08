#pragma once

#include <string>
#include <vector>

struct sqlite3;

struct PersistedPlayer {
    std::string username;
    std::string room;
    int exp = 0;
    int x = 2;
    int y = 2;
    std::vector<std::string> inventory;
};

class AuthDb {
public:
    explicit AuthDb(const std::string& db_path);
    ~AuthDb();

    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;

    bool verifyOrCreateUser(const std::string& username,
                            const std::string& password,
                            PersistedPlayer& out_player,
                            std::string& message);
    bool savePlayer(const PersistedPlayer& player);

private:
    sqlite3* db_ = nullptr;
};
