#pragma once

#include <string>

struct sqlite3;

class AuthDb {
public:
    explicit AuthDb(const std::string& db_path);
    ~AuthDb();

    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;

    bool verifyOrCreateUser(const std::string& username,
                            const std::string& password,
                            std::string& room,
                            int& exp,
                            std::string& message);

private:
    sqlite3* db_ = nullptr;
};
