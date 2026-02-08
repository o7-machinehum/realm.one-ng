#include "auth_db.h"

#include <sqlite3.h>

#include <stdexcept>

namespace {

void execOrThrow(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "sqlite exec failed";
        if (err) sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

} // namespace

AuthDb::AuthDb(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("failed to open sqlite db");
    }

    execOrThrow(db_,
        "CREATE TABLE IF NOT EXISTS players ("
        "username TEXT PRIMARY KEY,"
        "password TEXT NOT NULL,"
        "room TEXT NOT NULL DEFAULT 'd1.tmx',"
        "exp INTEGER NOT NULL DEFAULT 0"
        ");");
}

AuthDb::~AuthDb() {
    if (db_) sqlite3_close(db_);
}

bool AuthDb::verifyOrCreateUser(const std::string& username,
                                const std::string& password,
                                std::string& room,
                                int& exp,
                                std::string& message) {
    room = "d1.tmx";
    exp = 0;

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = "SELECT password, room, exp FROM players WHERE username = ?1;";
    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        message = "database prepare failed";
        return false;
    }

    sqlite3_bind_text(select_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(select_stmt);
    if (rc == SQLITE_ROW) {
        const char* db_pass = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 0));
        const char* db_room = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 1));
        exp = sqlite3_column_int(select_stmt, 2);

        const bool ok = (db_pass != nullptr) && (password == db_pass);
        if (db_room) room = db_room;

        sqlite3_finalize(select_stmt);

        if (!ok) {
            message = "invalid credentials";
            return false;
        }

        message = "welcome back";
        return true;
    }

    sqlite3_finalize(select_stmt);

    if (rc != SQLITE_DONE) {
        message = "database query failed";
        return false;
    }

    sqlite3_stmt* insert_stmt = nullptr;
    const char* insert_sql = "INSERT INTO players(username, password, room, exp) VALUES (?1, ?2, 'd1.tmx', 0);";
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
        message = "database prepare failed";
        return false;
    }

    sqlite3_bind_text(insert_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

    const int insert_rc = sqlite3_step(insert_stmt);
    sqlite3_finalize(insert_stmt);

    if (insert_rc != SQLITE_DONE) {
        message = "failed to create user";
        return false;
    }

    message = "new account created";
    return true;
}
