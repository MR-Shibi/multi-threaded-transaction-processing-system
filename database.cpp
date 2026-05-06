// ============================================================
//  database.cpp
//  SQLITE3 DATABASE LAYER — Two-tier connection model
//
//  Tier 1 (global):  db_init/close, producer, monitor calls.
//                    Protected by g_db_mutex.
//
//  Tier 2 (per-thread): validators and updaters open their own
//                    sqlite3* connection. WAL mode allows them
//                    to read/write without touching the mutex
//                    or blocking each other.
// ============================================================

#include "database.h"
#include "logger.h"

#include <sqlite3.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

// ============================================================
//  MODULE-LEVEL STATE (Tier 1 — global connection)
// ============================================================
static sqlite3*         g_db       = nullptr;
static pthread_mutex_t  g_db_mutex;

// ============================================================
//  HELPERS
// ============================================================
static void db_log(const std::string& msg) {
    logger_log(ThreadType::SYSTEM, 0, "[DB] " + msg);
}

// exec_simple_on(): run a SQL string against ANY sqlite3* handle.
// Caller must hold a lock if using g_db; no lock needed for per-thread conns.
static bool exec_simple_on(sqlite3* db, const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = "SQL error: ";
        err += (errmsg ? errmsg : "unknown");
        err += " | SQL: ";
        err += sql;
        db_log(err);
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

// Backward-compat wrapper: exec against the global connection (mutex held by caller)
static bool exec_simple(const char* sql) {
    return exec_simple_on(g_db, sql);
}

// ============================================================
//  db_init()
//  Opens the global connection, enables WAL, creates tables,
//  seeds test data. Call ONCE from main() before any threads.
// ============================================================
void db_init() {
    if (pthread_mutex_init(&g_db_mutex, nullptr) != 0) {
        db_log("FATAL: mutex init failed");
        return;
    }

    int rc = sqlite3_open(DB_FILE, &g_db);
    if (rc != SQLITE_OK) {
        db_log(std::string("FATAL: cannot open database: ")
               + sqlite3_errmsg(g_db));
        return;
    }
    db_log(std::string("Opened database: ") + DB_FILE);

    // WAL mode: readers never block writers, writers never block readers.
    // This is essential now that validators and updaters use separate connections.
    exec_simple("PRAGMA journal_mode=WAL;");
    exec_simple("PRAGMA foreign_keys=ON;");

    // Busy timeout: if two connections collide on a write, retry for
    // up to 5 seconds before returning SQLITE_BUSY.
    // Without this, concurrent writes get an immediate error.
    sqlite3_busy_timeout(g_db, 5000);

    exec_simple(
        "CREATE TABLE IF NOT EXISTS users ("
        "  user_id      INTEGER PRIMARY KEY,"
        "  name         TEXT    NOT NULL,"
        "  balance      REAL    NOT NULL DEFAULT 0.0,"
        "  credit_limit REAL    NOT NULL DEFAULT 500.0"
        ");"
    );

    exec_simple(
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  session_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id    INTEGER NOT NULL REFERENCES users(user_id),"
        "  is_active  INTEGER NOT NULL DEFAULT 1,"
        "  expires_at INTEGER NOT NULL"
        ");"
    );

    exec_simple(
        "CREATE TABLE IF NOT EXISTS raw_transactions ("
        "  txn_id    INTEGER PRIMARY KEY,"
        "  user_id   INTEGER NOT NULL,"
        "  amount    REAL    NOT NULL,"
        "  type      TEXT    NOT NULL,"
        "  status    TEXT    NOT NULL DEFAULT 'PENDING',"
        "  created_at INTEGER NOT NULL"
        ");"
    );

    exec_simple(
        "CREATE TABLE IF NOT EXISTS transactions ("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  txn_id        INTEGER NOT NULL,"
        "  user_id       INTEGER NOT NULL,"
        "  amount        REAL    NOT NULL,"
        "  type          TEXT    NOT NULL,"
        "  status        TEXT    NOT NULL DEFAULT 'PAID',"
        "  balance_after REAL    NOT NULL,"
        "  committed_at  INTEGER NOT NULL"
        ");"
    );

    db_log("All tables created (or already exist).");

    exec_simple(
        "INSERT OR IGNORE INTO users(user_id, name, balance, credit_limit) VALUES"
        "(1, 'Alice',   1500.00, 500.00),"
        "(2, 'Bob',     2200.00, 1000.00),"
        "(3, 'Charlie',  800.00, 300.00),"
        "(4, 'Diana',   3100.00, 1500.00),"
        "(5, 'Eve',      500.00, 200.00);"
    );

    exec_simple(
        "INSERT OR IGNORE INTO sessions(user_id, is_active, expires_at) VALUES"
        "(1, 1, 9999999999),"
        "(2, 1, 9999999999),"
        "(3, 1, 9999999999),"
        "(4, 1, 9999999999),"
        "(5, 0, 0);"
    );

    db_log("Test data seeded (users 1-4 active, user 5 logged out).");
    db_log("Database initialization complete.");
}

// ============================================================
//  db_close()
// ============================================================
void db_close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
        db_log("Global database connection closed.");
    }
    pthread_mutex_destroy(&g_db_mutex);
}

// ============================================================
//  db_open_connection()
//  Opens a fresh, independent SQLite connection for a thread.
//
//  Each Validator and Updater thread calls this at startup.
//  The returned sqlite3* belongs entirely to that thread —
//  no mutex is needed because no other thread touches it.
//
//  We enable WAL and set a busy_timeout so that the rare case
//  of two updaters writing at the exact same millisecond will
//  retry automatically rather than failing immediately.
// ============================================================
sqlite3* db_open_connection() {
    sqlite3* conn = nullptr;
    int rc = sqlite3_open(DB_FILE, &conn);
    if (rc != SQLITE_OK) {
        db_log(std::string("db_open_connection failed: ")
               + sqlite3_errmsg(conn));
        sqlite3_close(conn);
        return nullptr;
    }

    // Must re-enable WAL on every new connection.
    exec_simple_on(conn, "PRAGMA journal_mode=WAL;");
    exec_simple_on(conn, "PRAGMA foreign_keys=ON;");

    // Retry for up to 5 seconds if another writer is active.
    sqlite3_busy_timeout(conn, 5000);

    return conn;
}

// ============================================================
//  db_close_connection()
// ============================================================
void db_close_connection(sqlite3* conn) {
    if (conn) sqlite3_close(conn);
}

// ============================================================
//  db_execute()  — Tier 2 (per-thread)
//  Used by Updater threads to run pre-built SQL from the FIFO.
//  No global mutex — the caller's own sqlite3* is thread-local.
// ============================================================
bool db_execute(sqlite3* conn, const std::string& sql) {
    if (!conn) return false;
    return exec_simple_on(conn, sql.c_str());
}

// ============================================================
//  db_is_session_active()  — Tier 2 (per-thread)
//  Called by each Validator using its own connection.
// ============================================================
bool db_is_session_active(sqlite3* conn, int user_id) {
    if (!conn) return false;

    const char* sql =
        "SELECT COUNT(*) FROM sessions "
        "WHERE user_id = ? AND is_active = 1 AND expires_at > ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_is_session_active");
        return false;
    }

    sqlite3_bind_int(stmt,   1, user_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(nullptr));

    bool active = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        active = (sqlite3_column_int(stmt, 0) > 0);

    sqlite3_finalize(stmt);
    return active;
}

// ============================================================
//  db_get_balance()  — Tier 2 (per-thread)
//  Called by each Validator using its own connection.
// ============================================================
double db_get_balance(sqlite3* conn, int user_id) {
    if (!conn) return -1.0;

    const char* sql = "SELECT balance FROM users WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_get_balance");
        return -1.0;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    double balance = -1.0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        balance = sqlite3_column_double(stmt, 0);

    sqlite3_finalize(stmt);
    return balance;
}

// ============================================================
//  db_get_balance_global()  — Tier 1 (global, mutex held)
//  Convenience wrapper used by the Manual Producer wizard to
//  display the user's current balance during the input flow.
//  Not performance-critical — called once per user interaction.
// ============================================================
double db_get_balance_global(int user_id) {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT balance FROM users WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_get_balance_global");
        pthread_mutex_unlock(&g_db_mutex);
        return -1.0;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    double balance = -1.0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        balance = sqlite3_column_double(stmt, 0);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return balance;
}

// ============================================================
//  db_insert_raw_transaction()  — Tier 1 (global, mutex held)
//  Called by Producer threads.
// ============================================================
bool db_insert_raw_transaction(int txn_id, int user_id,
                               double amount,
                               const std::string& type) {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "INSERT INTO raw_transactions(txn_id, user_id, amount, type, status, created_at) "
        "VALUES(?, ?, ?, ?, 'PENDING', ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_insert_raw_transaction");
        pthread_mutex_unlock(&g_db_mutex);
        return false;
    }

    sqlite3_bind_int(stmt,    1, txn_id);
    sqlite3_bind_int(stmt,    2, user_id);
    sqlite3_bind_double(stmt, 3, amount);
    sqlite3_bind_text(stmt,   4, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,  5, (sqlite3_int64)time(nullptr));

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) db_log("INSERT into raw_transactions failed");

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return ok;
}

// ============================================================
//  db_update_raw_status()  — Tier 1 (global, mutex held)
//  Called by Validator to update audit status.
//  NOTE: Validators call this using the global connection because
//  status updates are short and infrequent. If this becomes a
//  bottleneck, pass the per-thread conn here too.
// ============================================================
bool db_update_raw_status(int txn_id, const std::string& status) {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "UPDATE raw_transactions SET status = ? WHERE txn_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_update_raw_status");
        pthread_mutex_unlock(&g_db_mutex);
        return false;
    }

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,  2, txn_id);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) db_log("UPDATE raw_transactions status failed for txn " +
                    std::to_string(txn_id));

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return ok;
}

// ============================================================
//  db_get_raw_status()  — Tier 1 (global, mutex held)
// ============================================================
std::string db_get_raw_status(int txn_id) {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT status FROM raw_transactions WHERE txn_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    std::string status = "UNKNOWN";

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, txn_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    } else {
        db_log("prepare failed in db_get_raw_status");
    }

    pthread_mutex_unlock(&g_db_mutex);
    return status;
}

// ============================================================
//  db_count_raw_by_status()  — Tier 1 (global, mutex held)
//  Used by Monitor thread.
// ============================================================
int db_count_raw_by_status(const std::string& status) {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "SELECT COUNT(*) FROM raw_transactions WHERE status = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

// ============================================================
//  db_count_committed()  — Tier 1 (global, mutex held)
//  Used by Monitor thread.
// ============================================================
int db_count_committed() {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT COUNT(*) FROM transactions;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}