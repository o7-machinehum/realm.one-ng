#include "auth_db.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace authdb {

namespace {

constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS players ("
    "  username   TEXT PRIMARY KEY,"
    "  public_key TEXT NOT NULL,"
    "  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
    ");";

// Extra columns added incrementally so an existing DB upgrades cleanly.
struct ColumnSpec {
    const char* name;
    const char* def_sql;
};
constexpr ColumnSpec kAdditiveColumns[] = {
    {"pos_x",   "INTEGER NOT NULL DEFAULT -1"},
    {"pos_y",   "INTEGER NOT NULL DEFAULT -1"},
    {"pos_z",   "INTEGER NOT NULL DEFAULT -1"},
    {"facing",  "INTEGER NOT NULL DEFAULT 0"},
};

constexpr const char* kSelectPubKeySql =
    "SELECT public_key FROM players WHERE username = ?1;";

constexpr const char* kInsertPlayerSql =
    "INSERT INTO players(username, public_key) VALUES (?1, ?2);";

constexpr const char* kSelectByPubKeySql =
    "SELECT 1 FROM players WHERE public_key = ?1 LIMIT 1;";

constexpr const char* kSelectStateSql =
    "SELECT pos_x, pos_y, pos_z, facing FROM players WHERE username = ?1;";

constexpr const char* kUpdateStateSql =
    "UPDATE players SET pos_x = ?1, pos_y = ?2, pos_z = ?3, facing = ?4 "
    "WHERE username = ?5;";

void execOrThrow(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "sqlite exec failed";
        if (err) sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) stmt_ = nullptr;
    }
    ~Stmt() { if (stmt_) sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return stmt_ != nullptr; }

    void bindText(int idx, const std::string& s) {
        sqlite3_bind_text(stmt_, idx, s.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bindInt(int idx, int v) { sqlite3_bind_int(stmt_, idx, v); }
    int  step()                  { return sqlite3_step(stmt_); }
    std::string columnText(int idx) {
        const auto* p = sqlite3_column_text(stmt_, idx);
        return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
    }
    int columnInt(int idx) { return sqlite3_column_int(stmt_, idx); }
private:
    sqlite3_stmt* stmt_ = nullptr;
};

bool columnExists(sqlite3* db, const std::string& table, const std::string& column) {
    Stmt q(db, ("PRAGMA table_info(" + table + ");").c_str());
    if (!q.ok()) return false;
    while (q.step() == SQLITE_ROW) {
        if (q.columnText(1) == column) return true;
    }
    return false;
}

void ensureColumn(sqlite3* db, const std::string& table, const ColumnSpec& spec) {
    if (columnExists(db, table, spec.name)) return;
    execOrThrow(db, std::string("ALTER TABLE ") + table +
                    " ADD COLUMN " + spec.name + " " + spec.def_sql + ";");
}

} // namespace

struct AuthDb::Impl {
    sqlite3* db = nullptr;

    explicit Impl(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("failed to open sqlite db: " + path);
        }
        execOrThrow(db, kCreateTableSql);
        for (const auto& col : kAdditiveColumns) {
            ensureColumn(db, "players", col);
        }
    }
    ~Impl() { if (db) sqlite3_close(db); }

    AuthOutcome existingPlayerCheck(const std::string& username,
                                    const std::string& pubkey,
                                    bool create_account,
                                    bool& player_existed) {
        Stmt q(db, kSelectPubKeySql);
        if (!q.ok()) return {false, "database prepare failed"};
        q.bindText(1, username);

        const int rc = q.step();
        if (rc == SQLITE_ROW) {
            player_existed = true;
            const std::string stored = q.columnText(0);
            if (create_account)        return {false, "username already exists"};
            if (stored != pubkey)      return {false, "public key mismatch"};
            return {true, "welcome back"};
        }
        if (rc != SQLITE_DONE)         return {false, "database query failed"};
        player_existed = false;
        return {false, "unknown username"};
    }

    bool publicKeyAlreadyTaken(const std::string& pubkey) {
        Stmt q(db, kSelectByPubKeySql);
        if (!q.ok()) return false;
        q.bindText(1, pubkey);
        return q.step() == SQLITE_ROW;
    }

    AuthOutcome createAccount(const std::string& username,
                              const std::string& pubkey) {
        if (publicKeyAlreadyTaken(pubkey)) {
            return {false, "public key already registered"};
        }
        Stmt ins(db, kInsertPlayerSql);
        if (!ins.ok()) return {false, "database prepare failed"};
        ins.bindText(1, username);
        ins.bindText(2, pubkey);
        if (ins.step() != SQLITE_DONE) return {false, "failed to create user"};
        return {true, "new account created"};
    }
};

AuthDb::AuthDb(const std::string& db_path)
    : impl_(std::make_unique<Impl>(db_path)) {}

AuthDb::~AuthDb() = default;

AuthOutcome AuthDb::tryLogin(const std::string& username,
                             const std::string& public_key_hex,
                             bool create_account) {
    if (username.empty() || public_key_hex.empty()) {
        return {false, "missing username or public key"};
    }
    bool existed = false;
    auto outcome = impl_->existingPlayerCheck(username, public_key_hex, create_account, existed);
    if (existed) return outcome;
    if (!create_account) return outcome;
    return impl_->createAccount(username, public_key_hex);
}

std::optional<PersistedState> AuthDb::loadState(const std::string& username) {
    Stmt q(impl_->db, kSelectStateSql);
    if (!q.ok()) return std::nullopt;
    q.bindText(1, username);
    if (q.step() != SQLITE_ROW) return std::nullopt;

    const int x = q.columnInt(0);
    const int y = q.columnInt(1);
    const int z = q.columnInt(2);
    if (x < 0 || y < 0 || z < 0) return std::nullopt;   // sentinel = never saved
    return PersistedState{
        x, y, z, static_cast<uint8_t>(q.columnInt(3))
    };
}

bool AuthDb::saveState(const std::string& username, const PersistedState& s) {
    Stmt up(impl_->db, kUpdateStateSql);
    if (!up.ok()) return false;
    up.bindInt (1, s.x);
    up.bindInt (2, s.y);
    up.bindInt (3, s.z);
    up.bindInt (4, static_cast<int>(s.facing));
    up.bindText(5, username);
    return up.step() == SQLITE_DONE;
}

} // namespace authdb
