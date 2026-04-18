#include "auth_db.h"

#include <sqlite3.h>

#include <algorithm>
#include <sstream>
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

bool columnExists(sqlite3* db, const std::string& table, const std::string& column) {
    std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    bool exists = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name && column == name) {
            exists = true;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return exists;
}

void ensureColumn(sqlite3* db, const std::string& table, const std::string& column, const std::string& def_sql) {
    if (!columnExists(db, table, column)) {
        execOrThrow(db, ("ALTER TABLE " + table + " ADD COLUMN " + column + " " + def_sql + ";").c_str());
    }
}

// Serialize vector<int64_t> as pipe-delimited int64 strings
std::string joinInventory(const std::vector<int64_t>& inv) {
    std::ostringstream oss;
    for (size_t i = 0; i < inv.size(); ++i) {
        if (i > 0) oss << "|";
        oss << inv[i];
    }
    return oss.str();
}

std::vector<int64_t> splitInventory(const std::string& s) {
    std::vector<int64_t> out;
    if (s.empty()) return out;

    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, '|')) {
        if (token.empty()) continue;
        try {
            out.push_back(std::stoll(token));
        } catch (...) {
            // Non-numeric value (old string format) — skip, migration handles this
        }
    }
    return out;
}

// Check if a string looks like the old string-based inventory format
bool isOldFormatInventory(const std::string& s) {
    if (s.empty()) return false;
    // Old format had item names like "Wooden Sword" separated by pipes
    // New format has int64 IDs. Check if first token is numeric.
    auto pipe = s.find('|');
    std::string first_token = (pipe != std::string::npos) ? s.substr(0, pipe) : s;
    if (first_token.empty()) return false;
    for (char c : first_token) {
        if (c != '-' && (c < '0' || c > '9')) return true; // contains non-numeric
    }
    return false;
}

std::string joinEquipment(const std::map<ItemType, int64_t>& m) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [k, iid] : m) {
        if (iid <= 0) continue;
        if (!first) oss << "|";
        first = false;
        oss << static_cast<int>(k) << "=" << iid;
    }
    return oss.str();
}

std::map<ItemType, int64_t> splitEquipment(const std::string& s) {
    std::map<ItemType, int64_t> out;
    if (s.empty()) return out;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, '|')) {
        if (token.empty()) continue;
        const auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = token.substr(0, eq);
        const std::string v = token.substr(eq + 1);
        if (k.empty() || v.empty()) continue;
        try {
            out[static_cast<ItemType>(std::stoi(k))] = std::stoll(v);
        } catch (...) {}
    }
    return out;
}

// Serialize item instances as id:def_id|id:def_id|...
std::string joinInstances(const std::vector<ItemInstance>& instances) {
    std::ostringstream oss;
    for (size_t i = 0; i < instances.size(); ++i) {
        if (i > 0) oss << "|";
        oss << instances[i].id << ":" << instances[i].def_id;
    }
    return oss.str();
}

std::vector<ItemInstance> splitInstances(const std::string& s) {
    std::vector<ItemInstance> out;
    if (s.empty()) return out;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, '|')) {
        if (token.empty()) continue;
        const auto colon = token.find(':');
        if (colon == std::string::npos) continue;
        ItemInstance inst;
        try {
            inst.id = std::stoll(token.substr(0, colon));
        } catch (...) { continue; }
        inst.def_id = token.substr(colon + 1);
        if (!inst.def_id.empty()) out.push_back(std::move(inst));
    }
    return out;
}

std::string joinSkills(const PersistedPlayer& p) {
    std::ostringstream oss;
    oss << "melee=" << p.melee_xp
        << "|distance=" << p.distance_xp
        << "|magic=" << p.magic_xp
        << "|shielding=" << p.shielding_xp
        << "|evasion=" << p.evasion_xp;
    return oss.str();
}

void splitSkills(const std::string& s, PersistedPlayer& p) {
    if (s.empty()) return;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, '|')) {
        if (token.empty()) continue;
        const auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = token.substr(0, eq);
        const std::string v = token.substr(eq + 1);
        if (k.empty() || v.empty()) continue;
        int n = 0;
        try { n = std::stoi(v); } catch (...) { continue; }
        if (k == "melee") p.melee_xp = std::max(0, n);
        else if (k == "distance") p.distance_xp = std::max(0, n);
        else if (k == "magic") p.magic_xp = std::max(0, n);
        else if (k == "shielding") p.shielding_xp = std::max(0, n);
        else if (k == "evasion") p.evasion_xp = std::max(0, n);
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
        "password TEXT NOT NULL DEFAULT '',"
        "public_key TEXT NOT NULL DEFAULT '',"
        "room TEXT NOT NULL DEFAULT '500_500:0.tmx',"
        "exp INTEGER NOT NULL DEFAULT 0"
        ");");

    ensureColumn(db_, "players", "public_key", "TEXT NOT NULL DEFAULT ''");
    ensureColumn(db_, "players", "x", "INTEGER NOT NULL DEFAULT 16");
    ensureColumn(db_, "players", "y", "INTEGER NOT NULL DEFAULT 10");
    ensureColumn(db_, "players", "inventory", "TEXT NOT NULL DEFAULT ''");
    ensureColumn(db_, "players", "equipment", "TEXT NOT NULL DEFAULT ''");
    ensureColumn(db_, "players", "skills", "TEXT NOT NULL DEFAULT ''");
    ensureColumn(db_, "players", "item_instances", "TEXT NOT NULL DEFAULT ''");
}

AuthDb::~AuthDb() {
    if (db_) sqlite3_close(db_);
}

bool AuthDb::loginWithPublicKey(const std::string& username,
                                const std::string& public_key_hex,
                                bool create_account,
                                PersistedPlayer& out_player,
                                std::string& message) {
    out_player = PersistedPlayer{};
    out_player.username = username;
    if (username.empty() || public_key_hex.empty()) {
        message = "missing username or public key";
        return false;
    }

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = "SELECT public_key, room, exp, x, y, inventory, equipment, skills, item_instances FROM players WHERE username = ?1;";
    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        message = "database prepare failed";
        return false;
    }

    sqlite3_bind_text(select_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(select_stmt);
    if (rc == SQLITE_ROW) {
        const char* db_pub = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 0));
        const char* db_room = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 1));
        out_player.exp = sqlite3_column_int(select_stmt, 2);
        out_player.pos.x = sqlite3_column_int(select_stmt, 3);
        out_player.pos.y = sqlite3_column_int(select_stmt, 4);
        const char* inv = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 5));
        const char* eqp = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 6));
        const char* sks = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 7));
        const char* inst = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 8));

        const bool ok = (db_pub != nullptr) && (public_key_hex == db_pub);
        if (db_room) out_player.room = db_room;
        if (inst) out_player.owned_instances = splitInstances(inst);
        if (inv) {
            const std::string inv_str = inv;
            if (isOldFormatInventory(inv_str)) {
                // Migration: old string-based inventory -> GID-based
                // We need to allocate IDs; use a counter starting from max existing
                int64_t next_id = 1;
                for (const auto& existing : out_player.owned_instances) {
                    if (existing.id >= next_id) next_id = existing.id + 1;
                }
                std::string token;
                std::istringstream iss(inv_str);
                while (std::getline(iss, token, '|')) {
                    if (token.empty()) continue;
                    ItemInstance inst_new;
                    inst_new.id = next_id++;
                    inst_new.def_id = token;
                    out_player.owned_instances.push_back(inst_new);
                    out_player.inventory.push_back(inst_new.id);
                }
                // Old equipment used inventory indices; clear it since indices are meaningless now
                out_player.equipment_by_type.clear();
            } else {
                out_player.inventory = splitInventory(inv_str);
            }
        }
        if (eqp && !isOldFormatInventory(inv ? inv : "")) {
            out_player.equipment_by_type = splitEquipment(eqp);
        }
        if (sks) splitSkills(sks, out_player);

        sqlite3_finalize(select_stmt);

        if (create_account) {
            message = "username already exists";
            return false;
        }
        if (!ok) {
            message = "public key mismatch";
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

    if (!create_account) {
        message = "unknown username";
        return false;
    }

    sqlite3_stmt* check_pub_stmt = nullptr;
    const char* check_pub_sql = "SELECT 1 FROM players WHERE public_key = ?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db_, check_pub_sql, -1, &check_pub_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(check_pub_stmt, 1, public_key_hex.c_str(), -1, SQLITE_TRANSIENT);
        const int pub_rc = sqlite3_step(check_pub_stmt);
        sqlite3_finalize(check_pub_stmt);
        if (pub_rc == SQLITE_ROW) {
            message = "public key already registered";
            return false;
        }
    }

    out_player.room = "500_500:0.tmx";
    out_player.exp = 0;
    out_player.pos = {16, 10};
    out_player.melee_xp = 0;
    out_player.distance_xp = 0;
    out_player.magic_xp = 0;
    out_player.shielding_xp = 0;
    out_player.evasion_xp = 0;
    out_player.inventory.clear();

    sqlite3_stmt* insert_stmt = nullptr;
    const char* insert_sql =
        "INSERT INTO players(username, password, public_key, room, exp, x, y, inventory, skills, item_instances) "
        "VALUES (?1, '', ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);";
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK) {
        message = "database prepare failed";
        return false;
    }

    const std::string inv_s = joinInventory(out_player.inventory);
    const std::string sks_s = joinSkills(out_player);
    const std::string inst_s = joinInstances(out_player.owned_instances);
    sqlite3_bind_text(insert_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 2, public_key_hex.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 3, out_player.room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insert_stmt, 4, out_player.exp);
    sqlite3_bind_int(insert_stmt, 5, out_player.pos.x);
    sqlite3_bind_int(insert_stmt, 6, out_player.pos.y);
    sqlite3_bind_text(insert_stmt, 7, inv_s.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 8, sks_s.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_stmt, 9, inst_s.c_str(), -1, SQLITE_TRANSIENT);

    const int insert_rc = sqlite3_step(insert_stmt);
    sqlite3_finalize(insert_stmt);

    if (insert_rc != SQLITE_DONE) {
        message = "failed to create user";
        return false;
    }

    message = "new account created";
    return true;
}

bool AuthDb::savePlayer(const PersistedPlayer& player) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE players SET room = ?1, exp = ?2, x = ?3, y = ?4, inventory = ?5, equipment = ?6, skills = ?7, item_instances = ?8 WHERE username = ?9;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    const std::string inv = joinInventory(player.inventory);
    const std::string eqp = joinEquipment(player.equipment_by_type);
    const std::string sks = joinSkills(player);
    const std::string inst = joinInstances(player.owned_instances);
    sqlite3_bind_text(stmt, 1, player.room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, player.exp);
    sqlite3_bind_int(stmt, 3, player.pos.x);
    sqlite3_bind_int(stmt, 4, player.pos.y);
    sqlite3_bind_text(stmt, 5, inv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, eqp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, sks.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, inst.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, player.username.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
