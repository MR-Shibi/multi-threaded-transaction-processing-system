#include "database.h"
#include "logger.h"

#include <sqlite3.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

// All SQLite file access is serialized through one POSIX mutex (showcase primitive).
static pthread_mutex_t g_db_mutex;
static sqlite3* g_db = nullptr;

// Utility to log database-related messages to the system logger in a thread-safe manner.
static void db_log(const std::string& msg) {
    logger_log(ThreadType::SYSTEM, 0, "[DB] " + msg);
}

// Executes a simple SQL command on a specific connection; requires external synchronization if connection is shared.
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

void db_init() {
    pthread_mutex_init(&g_db_mutex, nullptr);

    // Extra safety: clean up any stale database files before opening.
    remove(DB_FILE);
    remove("transactions.db-wal");
    remove("transactions.db-shm");

    int rc = sqlite3_open(DB_FILE, &g_db);
    if (rc != SQLITE_OK) {
        db_log("FATAL: Could not open database file.");
        return;
    }
    sqlite3_busy_timeout(g_db, 5000);

    // WAL: readers (validators) can overlap writers (updaters) at the OS/DB level;
    // we still serialize access with g_db_mutex so the demo clearly shows mutex discipline.
    const char* setup_queries[] = {
        "PRAGMA journal_mode=WAL;",
        "PRAGMA synchronous=NORMAL;",
        "PRAGMA foreign_keys=OFF;",

        "BEGIN TRANSACTION;",
        
        // Schema creation
        "CREATE TABLE users (user_id INTEGER PRIMARY KEY, name TEXT NOT NULL, balance REAL NOT NULL, credit_limit REAL NOT NULL);",
        "CREATE TABLE sessions (session_id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL, is_active INTEGER NOT NULL, expires_at INTEGER NOT NULL);",
        "CREATE TABLE raw_transactions (txn_id INTEGER PRIMARY KEY, user_id INTEGER NOT NULL, amount REAL NOT NULL, type TEXT NOT NULL, status TEXT NOT NULL, created_at INTEGER NOT NULL);",
        "CREATE TABLE transactions (id INTEGER PRIMARY KEY AUTOINCREMENT, txn_id INTEGER NOT NULL, user_id INTEGER NOT NULL, amount REAL NOT NULL, type TEXT NOT NULL, status TEXT NOT NULL, balance_after REAL NOT NULL, committed_at INTEGER NOT NULL);",
        
        // Seed data
        "INSERT INTO users VALUES (1, 'Alice', 1500.0, 500.0);",
        "INSERT INTO users VALUES (2, 'Bob', 2200.0, 1000.0);",
        "INSERT INTO users VALUES (3, 'Charlie', 800.0, 300.0);",
        "INSERT INTO users VALUES (4, 'Diana', 3100.0, 1500.0);",
        "INSERT INTO users VALUES (5, 'Eve', 500.0, 200.0);",
        
        "INSERT INTO sessions (user_id, is_active, expires_at) VALUES (1, 1, 9999999999);",
        "INSERT INTO sessions (user_id, is_active, expires_at) VALUES (2, 1, 9999999999);",
        "INSERT INTO sessions (user_id, is_active, expires_at) VALUES (3, 1, 9999999999);",
        "INSERT INTO sessions (user_id, is_active, expires_at) VALUES (4, 1, 9999999999);",
        "INSERT INTO sessions (user_id, is_active, expires_at) VALUES (5, 0, 0);",
        
        "COMMIT;",
        
        "PRAGMA foreign_keys=ON;"
    };

    for (const char* q : setup_queries) {
        if (!exec_simple_on(g_db, q)) {
            db_log(std::string("CRITICAL: Init query failed: ") + q);
        }
    }

    db_log("Database initialization successfully completed.");
}

// Closes the global database connection and destroys the global mutex to free OS synchronization resources.
void db_close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
        db_log("Global database connection closed.");
    }
    pthread_mutex_destroy(&g_db_mutex);
}

// Opens a new thread-local database connection to allow concurrent database access without blocking the global mutex.
sqlite3* db_open_connection() {
    sqlite3* conn = nullptr;
    int rc = sqlite3_open(DB_FILE, &conn);
    if (rc != SQLITE_OK) {
        db_log(std::string("db_open_connection failed: ")
               + sqlite3_errmsg(conn));
        sqlite3_close(conn);
        return nullptr;
    }

    sqlite3_busy_timeout(conn, 5000);

    pthread_mutex_lock(&g_db_mutex);
    exec_simple_on(conn, "PRAGMA foreign_keys=ON;");
    pthread_mutex_unlock(&g_db_mutex);

    return conn;
}

// Closes a thread-local database connection, releasing the associated file descriptors and memory.
void db_close_connection(sqlite3* conn) {
    if (conn) sqlite3_close(conn);
}

// Executes a SQL command on a provided connection; caller must ensure thread safety for the specific connection.
bool db_execute(sqlite3* conn, const std::string& sql) {
    if (!conn) return false;
    pthread_mutex_lock(&g_db_mutex);
    bool ok = exec_simple_on(conn, sql.c_str());
    pthread_mutex_unlock(&g_db_mutex);
    return ok;
}

// Checks if a user session is active using a thread-local connection to avoid contention on the global mutex.
bool db_is_session_active(sqlite3* conn, int user_id) {
    if (!conn) return false;

    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "SELECT COUNT(*) FROM sessions "
        "WHERE user_id = ? AND is_active = 1 AND expires_at > ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_is_session_active");
        pthread_mutex_unlock(&g_db_mutex);
        return false;
    }

    sqlite3_bind_int(stmt,   1, user_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(nullptr));

    bool active = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        active = (sqlite3_column_int(stmt, 0) > 0);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return active;
}

// Retrieves user balance using a thread-local connection for efficient concurrent reads.
double db_get_balance(sqlite3* conn, int user_id) {
    if (!conn) return -1.0;

    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT balance FROM users WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_get_balance");
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

// Retrieves user balance using the global handle, protected by a mutex to prevent race conditions.
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

// Inserts a new transaction record into the database, synchronized with a mutex to ensure atomic writes.
bool db_insert_raw_transaction(sqlite3* conn, int txn_id, int user_id,
                               double amount,
                               const std::string& type) {
    if (!conn) conn = g_db;
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "INSERT INTO raw_transactions(txn_id, user_id, amount, type, status, created_at) "
        "VALUES(?, ?, ?, ?, 'PENDING', ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_insert_raw_transaction: " + std::string(sqlite3_errmsg(conn)));
        pthread_mutex_unlock(&g_db_mutex);
        return false;
    }

    sqlite3_bind_int(stmt,    1, txn_id);
    sqlite3_bind_int(stmt,    2, user_id);
    sqlite3_bind_double(stmt, 3, amount);
    sqlite3_bind_text(stmt,   4, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,  5, (sqlite3_int64)time(nullptr));

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) db_log("INSERT into raw_transactions failed: " + std::string(sqlite3_errmsg(conn)));

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return ok;
}

// Updates the status of a transaction, using a mutex to ensure data consistency across multiple threads.
bool db_update_raw_status(sqlite3* conn, int txn_id, const std::string& status) {
    if (!conn) conn = g_db;
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "UPDATE raw_transactions SET status = ? WHERE txn_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_update_raw_status: " + std::string(sqlite3_errmsg(conn)));
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

// Queries the status of a transaction from the shared database, protected by a mutex.
std::string db_get_raw_status(sqlite3* conn, int txn_id) {
    if (!conn) conn = g_db;
    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT status FROM raw_transactions WHERE txn_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    std::string status = "UNKNOWN";

    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
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

// Counts transactions with a specific status; uses a mutex to provide a consistent snapshot of the data.
int db_count_raw_by_status(sqlite3* conn, const std::string& status) {
    if (!conn) conn = g_db;
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "SELECT COUNT(*) FROM raw_transactions WHERE status = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
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

// Counts transactions by type; synchronization via mutex prevents reading stale or partial data during updates.
int db_count_raw_by_type(sqlite3* conn, const std::string& type) {
    if (!conn) conn = g_db;
    pthread_mutex_lock(&g_db_mutex);

    const char* sql =
        "SELECT COUNT(*) FROM raw_transactions WHERE type = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

// Returns the total number of committed transactions; uses a mutex to ensure thread-safe access to the global handle.
int db_count_committed(sqlite3* conn) {
    if (!conn) conn = g_db;
    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT COUNT(*) FROM transactions;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
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