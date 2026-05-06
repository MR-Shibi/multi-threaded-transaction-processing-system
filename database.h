#ifndef DATABASE_H
#define DATABASE_H

// ============================================================
//  database.h
//  SQLITE3 DATABASE LAYER
//
//  Two-tier connection model:
//
//  1. Global connection (g_db) — used by Producer, Monitor, and
//     db_init/db_close. Protected by a single mutex.
//     Good enough for low-frequency calls.
//
//  2. Per-thread connections — used by Validator and Updater
//     threads via db_open_connection() / db_close_connection().
//     Each thread holds its own sqlite3* in a local variable.
//     WAL mode allows these to run concurrently without blocking
//     each other or the global connection.
//
//  WHY THIS MATTERS:
//    The old code put a global mutex around every SQLite call,
//    which completely serialized all DB access even though WAL
//    mode supports concurrent readers + one writer. Under load,
//    threads spent most of their time waiting for that one lock.
//
//  Tables:
//    users            — registered users with balances
//    sessions         — active login sessions
//    raw_transactions — audit log (every transaction, any status)
//    transactions     — final committed records (PAID only)
// ============================================================

#include <string>
#include <sqlite3.h>

// Path to the SQLite database file on disk.
static constexpr const char* DB_FILE = "transactions.db";

// ── Lifecycle (global connection) ────────────────────────────

// Opens the database, enables WAL mode, creates tables, seeds data.
// Call ONCE from main() before starting any threads.
void db_init();

// Closes the global connection cleanly.
// Call ONCE from main() after all threads have been joined.
void db_close();

// ── Per-thread connection API ────────────────────────────────

// Opens a new, independent SQLite connection for the calling thread.
// Each Validator and Updater thread should call this at startup
// and store the returned handle as a local variable.
// Returns nullptr on failure.
sqlite3* db_open_connection();

// Closes a per-thread connection. Call at the end of the thread function.
void db_close_connection(sqlite3* conn);

// ── General execution (per-thread) ──────────────────────────

// Executes an arbitrary SQL string using the provided connection.
// Used by DB Updater threads. No global mutex — callers serialize
// themselves via the FIFO queue ordering.
// Returns true on success, false on failure.
bool db_execute(sqlite3* conn, const std::string& sql);

// ── Validator queries (per-thread connection) ─────────────────

// Returns true if the user has an active, non-expired session.
bool db_is_session_active(sqlite3* conn, int user_id);

// Returns the user's current balance. Returns -1.0 on error.
double db_get_balance(sqlite3* conn, int user_id);

// ── Producer / Monitor queries (global connection) ───────────
// These still use the global mutex-protected connection since
// they are called infrequently.

// Inserts a new row into raw_transactions with status PENDING.
bool db_insert_raw_transaction(int txn_id, int user_id,
                               double amount,
                               const std::string& type);

// Updates a raw_transaction's status (PROCESSING, DONE, REJECTED).
bool db_update_raw_status(int txn_id, const std::string& status);

// Returns the current status of a raw_transaction.
std::string db_get_raw_status(int txn_id);

// Returns count of rows in raw_transactions with given status.
int db_count_raw_by_status(const std::string& status);

// Returns total count of committed transactions.
int db_count_committed();

// Returns a user's current balance via the global connection.
// Used by the Manual Producer wizard to display balance in the UI.
// (Tier-1 convenience overload — mutex-protected.)
double db_get_balance_global(int user_id);

#endif // DATABASE_H