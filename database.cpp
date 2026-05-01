// ============================================================
//  database.cpp
//  SQLITE3 DATABASE LAYER — Full Implementation
//
//  All database access in the project goes through this file.
//  One connection, one mutex, clean API for all thread types.
// ============================================================

#include "database.h"
#include "logger.h"

#include <sqlite3.h>      // SQLite3 C API
#include <pthread.h>      // pthread_mutex_t
#include <cstdio>         // snprintf
#include <cstring>        // strlen
#include <string>

// ============================================================
//  MODULE-LEVEL STATE (private to this file)
// ============================================================

// The single shared database connection.
// sqlite3* is an opaque pointer — we never look inside it.
// All SQLite calls take this as their first argument.
static sqlite3* g_db = nullptr;

// Mutex protecting every SQLite call.
// SQLite in "serialized" mode has some internal safety, but
// an explicit mutex gives us clear, predictable control and
// ensures our read-modify-write sequences (like check balance
// then update balance) happen atomically from our perspective.
static pthread_mutex_t g_db_mutex;

// ============================================================
//  HELPER: db_log()
//  Sends a message to the logger using SYSTEM thread type.
//  Avoids repeating the logger_log() boilerplate everywhere.
// ============================================================
static void db_log(const std::string& msg) {
    logger_log(ThreadType::SYSTEM, 0, "[DB] " + msg);
}

// ============================================================
//  HELPER: exec_simple()
//  Executes a SQL string with no return value needed.
//  Used for CREATE TABLE, PRAGMA, INSERT seeds, etc.
//  Must be called with the mutex already held.
// ============================================================
static bool exec_simple(const char* sql) {
    char* errmsg = nullptr;

    // sqlite3_exec() compiles, runs, and finalizes the SQL in one call.
    // The callback (3rd arg) is for SELECT results — we pass NULL here
    // because we don't need to read results in this helper.
    // errmsg is filled in if something goes wrong.
    int rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &errmsg);

    if (rc != SQLITE_OK) {
        std::string err = "exec_simple failed: ";
        err += (errmsg ? errmsg : "unknown error");
        err += " | SQL: ";
        err += sql;
        db_log(err);

        // sqlite3_free() must be called on errmsg when non-null.
        // It was allocated by the SQLite library and only SQLite
        // knows how to free it correctly.
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }

    return true;
}

// ============================================================
//  db_init()
//  Opens the database file, enables WAL mode, creates all
//  tables, and seeds test data.
//
//  WHAT IS WAL MODE?
//  Default SQLite locking: one writer blocks ALL readers.
//  WAL (Write-Ahead Logging): writers write to a separate
//  .wal file. Readers see the last committed snapshot and
//  are never blocked by a concurrent writer. This is critical
//  for our system where validators read while updaters write.
// ============================================================
void db_init() {
    // ── Initialize the mutex first ───────────────────────────
    if (pthread_mutex_init(&g_db_mutex, nullptr) != 0) {
        db_log("FATAL: mutex init failed");
        return;
    }

    // ── Open (or create) the database file ───────────────────
    // sqlite3_open() creates the file if it doesn't exist.
    // Returns SQLITE_OK (0) on success.
    int rc = sqlite3_open(DB_FILE, &g_db);
    if (rc != SQLITE_OK) {
        db_log(std::string("FATAL: cannot open database: ")
               + sqlite3_errmsg(g_db));
        return;
    }
    db_log(std::string("Opened database: ") + DB_FILE);

    // ── Enable WAL mode ──────────────────────────────────────
    // PRAGMA is a SQLite-specific command for configuration.
    // WAL mode persists in the database file — we only need
    // to set it once, but setting it every startup is harmless.
    exec_simple("PRAGMA journal_mode=WAL;");

    // ── Enable foreign keys ──────────────────────────────────
    // SQLite doesn't enforce foreign keys by default — must opt in.
    exec_simple("PRAGMA foreign_keys=ON;");

    // ── Create tables ────────────────────────────────────────
    // IF NOT EXISTS means running this on a database that already
    // has the tables is safe — it's a no-op, not an error.

    // USERS table: registered users with balances
    exec_simple(
        "CREATE TABLE IF NOT EXISTS users ("
        "  user_id      INTEGER PRIMARY KEY,"
        "  name         TEXT    NOT NULL,"
        "  balance      REAL    NOT NULL DEFAULT 0.0,"
        "  credit_limit REAL    NOT NULL DEFAULT 500.0"
        ");"
    );

    // SESSIONS table: tracks who is currently logged in.
    // is_active: 1 = logged in, 0 = logged out.
    // expires_at: Unix timestamp. Validator checks time(nullptr) < expires_at.
    exec_simple(
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  session_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id    INTEGER NOT NULL REFERENCES users(user_id),"
        "  is_active  INTEGER NOT NULL DEFAULT 1,"
        "  expires_at INTEGER NOT NULL"   // Unix timestamp
        ");"
    );

    // RAW_TRANSACTIONS: audit log — every transaction that ever
    // entered Buffer 1, regardless of outcome.
    // status progression: PENDING → PROCESSING → DONE | REJECTED
    exec_simple(
        "CREATE TABLE IF NOT EXISTS raw_transactions ("
        "  txn_id    INTEGER PRIMARY KEY,"
        "  user_id   INTEGER NOT NULL,"
        "  amount    REAL    NOT NULL,"
        "  type      TEXT    NOT NULL,"
        "  status    TEXT    NOT NULL DEFAULT 'PENDING',"
        "  created_at INTEGER NOT NULL"   // Unix timestamp
        ");"
    );

    // TRANSACTIONS: final record — only successfully committed ones.
    // A row here means the money actually moved.
    exec_simple(
        "CREATE TABLE IF NOT EXISTS transactions ("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  txn_id        INTEGER NOT NULL,"
        "  user_id       INTEGER NOT NULL,"
        "  amount        REAL    NOT NULL,"
        "  type          TEXT    NOT NULL,"
        "  status        TEXT    NOT NULL DEFAULT 'PAID',"
        "  balance_after REAL    NOT NULL,"
        "  committed_at  INTEGER NOT NULL"   // Unix timestamp
        ");"
    );

    db_log("All tables created (or already exist).");

    // ── Seed test data ────────────────────────────────────────
    // INSERT OR IGNORE: if the row already exists (by PRIMARY KEY),
    // skip it silently. This makes the seed idempotent — safe to
    // call every time the program starts without duplicating data.

    exec_simple(
        "INSERT OR IGNORE INTO users(user_id, name, balance, credit_limit) VALUES"
        "(1, 'Alice',   1500.00, 500.00),"
        "(2, 'Bob',     2200.00, 1000.00),"
        "(3, 'Charlie',  800.00, 300.00),"
        "(4, 'Diana',   3100.00, 1500.00),"
        "(5, 'Eve',      500.00, 200.00);"
    );

    // Sessions: users 1-4 are logged in (active), user 5 is logged out.
    // expires_at = now + 3600 seconds (1 hour from startup).
    // We use strftime in SQL to get a far-future expiry for testing.
    // The literal 9999999999 is year 2286 — effectively "never expires".
    exec_simple(
        "INSERT OR IGNORE INTO sessions(user_id, is_active, expires_at) VALUES"
        "(1, 1, 9999999999),"   // Alice   — active
        "(2, 1, 9999999999),"   // Bob     — active
        "(3, 1, 9999999999),"   // Charlie — active
        "(4, 1, 9999999999),"   // Diana   — active
        "(5, 0, 0);"            // Eve     — logged OUT (for rejection testing)
    );

    db_log("Test data seeded (users 1-4 active, user 5 logged out).");
    db_log("Database initialization complete.");
}

// ============================================================
//  db_close()
//  Closes the connection. Call after all threads are done.
// ============================================================
void db_close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
        db_log("Database connection closed.");
    }
    pthread_mutex_destroy(&g_db_mutex);
}

// ============================================================
//  db_execute()
//  Runs an arbitrary SQL string. Used by DB Updater threads
//  to execute the pre-built commit_query from the Transaction.
//
//  This is the function that makes the DB Updater so simple —
//  it just passes the string it received from the FIFO and
//  this function handles everything else.
// ============================================================
bool db_execute(const std::string& sql) {
    pthread_mutex_lock(&g_db_mutex);
    bool ok = exec_simple(sql.c_str());
    pthread_mutex_unlock(&g_db_mutex);
    return ok;
}

// ============================================================
//  db_is_session_active()
//  Called by Validator threads to check if a user is logged in.
//
//  Uses a PREPARED STATEMENT:
//  Instead of building the SQL string by concatenation
//  ("SELECT ... WHERE user_id = " + to_string(user_id))
//  we compile the SQL once with a '?' placeholder, then bind
//  the actual value. This is safer and faster.
//
//  sqlite3 prepared statement lifecycle:
//    prepare_v2() → bind_*() → step() → finalize()
// ============================================================
bool db_is_session_active(int user_id) {
    pthread_mutex_lock(&g_db_mutex);

    // The SQL we want to run. '?' is the placeholder for user_id.
    // We check: is_active=1 AND expires_at > current Unix time.
    const char* sql =
        "SELECT COUNT(*) FROM sessions "
        "WHERE user_id = ? AND is_active = 1 AND expires_at > ?;";

    // sqlite3_stmt* is the compiled (prepared) statement object.
    sqlite3_stmt* stmt = nullptr;

    // sqlite3_prepare_v2() compiles the SQL into bytecode.
    // -1 means "read until the null terminator".
    // &stmt receives the compiled statement handle.
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_is_session_active");
        pthread_mutex_unlock(&g_db_mutex);
        return false;
    }

    // sqlite3_bind_int() fills in the first '?' with user_id.
    // The '1' means "bind to the 1st placeholder".
    sqlite3_bind_int(stmt, 1, user_id);

    // Bind current time to the second '?' (for expires_at check).
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(nullptr));

    // sqlite3_step() executes one row of the result.
    // SQLITE_ROW means a result row is ready to read.
    bool active = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // sqlite3_column_int() reads the first column (index 0) as int.
        // COUNT(*) returns 0 or 1 here.
        active = (sqlite3_column_int(stmt, 0) > 0);
    }

    // sqlite3_finalize() frees the prepared statement.
    // MUST be called to avoid memory leaks.
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&g_db_mutex);
    return active;
}

// ============================================================
//  db_get_balance()
//  Returns a user's current balance.
//  Called by Validator to check if withdrawal amount is valid.
// ============================================================
double db_get_balance(int user_id) {
    pthread_mutex_lock(&g_db_mutex);

    const char* sql = "SELECT balance FROM users WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_log("prepare failed in db_get_balance");
        pthread_mutex_unlock(&g_db_mutex);
        return -1.0;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    double balance = -1.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // sqlite3_column_double() reads the column as a double.
        balance = sqlite3_column_double(stmt, 0);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return balance;
}

// ============================================================
//  db_insert_raw_transaction()
//  Called by Producer threads to record every new transaction
//  in the audit log table with status PENDING.
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

    // Bind all four parameters in order
    sqlite3_bind_int(stmt,    1, txn_id);
    sqlite3_bind_int(stmt,    2, user_id);
    sqlite3_bind_double(stmt, 3, amount);
    sqlite3_bind_text(stmt,   4, type.c_str(), -1, SQLITE_TRANSIENT);
    // SQLITE_TRANSIENT tells SQLite to make its own copy of the string.
    // (As opposed to SQLITE_STATIC which assumes the string lives forever.)
    sqlite3_bind_int64(stmt,  5, (sqlite3_int64)time(nullptr));

    // sqlite3_step() on an INSERT returns SQLITE_DONE (not SQLITE_ROW)
    // when the insert completes successfully.
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) db_log("INSERT into raw_transactions failed");

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return ok;
}

// ============================================================
//  db_update_raw_status()
//  Updates the status of a raw_transaction.
//  Called by Validator to mark transactions as PROCESSING,
//  then DONE or REJECTED.
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
//  db_count_raw_by_status()
//  Used by Monitor thread to count transactions in each status.
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
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}

// ============================================================
//  db_count_committed()
//  Used by Monitor thread to count fully committed transactions.
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
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return count;
}