#ifndef DATABASE_H
#define DATABASE_H

// ============================================================
//  database.h
//  SQLITE3 DATABASE LAYER
//
//  One persistent connection, one mutex.
//  All threads go through this module — never touch SQLite directly.
//
//  Tables:
//    users            — registered users with balances
//    sessions         — active login sessions
//    raw_transactions — audit log (every transaction, any status)
//    transactions     — final committed records (PAID only)
// ============================================================

#include <string>

// Path to the SQLite database file on disk.
// Created automatically on first run. Persists across runs.
static const char* DB_FILE = "transactions.db";

// ── Lifecycle ────────────────────────────────────────────────

// Opens the database, enables WAL mode, creates tables if they
// don't exist, and seeds test users and sessions.
// Call ONCE from main() before starting any threads.
void db_init();

// Closes the database connection cleanly.
// Call ONCE from main() after all threads have been joined.
void db_close();

// ── General execution ────────────────────────────────────────

// Executes an arbitrary SQL string (used by DB Updater threads).
// Holds the mutex for the duration of the call.
// Returns true on success, false on failure (logs the error).
bool db_execute(const std::string& sql);

// ── Validator queries (read-only) ────────────────────────────

// Returns true if the user has an active, non-expired session.
bool db_is_session_active(int user_id);

// Returns the user's current balance. Returns -1.0 on error.
double db_get_balance(int user_id);

// ── Producer queries ─────────────────────────────────────────

// Inserts a new row into raw_transactions with status PENDING.
bool db_insert_raw_transaction(int txn_id, int user_id,
                               double amount,
                               const std::string& type);

// ── Validator update ─────────────────────────────────────────

// Updates a raw_transaction's status (PROCESSING, DONE, REJECTED).
bool db_update_raw_status(int txn_id, const std::string& status);

// ── Monitor queries ──────────────────────────────────────────

// Returns count of rows in raw_transactions with given status.
int db_count_raw_by_status(const std::string& status);

// Returns total count of committed transactions.
int db_count_committed();

#endif // DATABASE_H