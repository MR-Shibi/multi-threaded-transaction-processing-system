#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>

static constexpr const char* DB_FILE = "transactions.db";

// Initializes the global database handle and a process-wide pthread mutex for DB access.
void db_init();
// Closes the global database connection and destroys the mutex.
void db_close();

// Opens a thread-local SQLite connection (WAL + foreign_keys are applied at open).
sqlite3* db_open_connection();
// Closes a thread-local database connection, releasing system resources.
void db_close_connection(sqlite3* conn);

// Executes a SQL command on the provided connection (serialized with the global DB mutex).
bool db_execute(sqlite3* conn, const std::string& sql);

// Checks if a user's session is active in the database.
bool db_is_session_active(sqlite3* conn, int user_id);
// Retrieves the current balance for a user from the database.
double db_get_balance(sqlite3* conn, int user_id);

bool db_insert_raw_transaction(sqlite3* conn, int txn_id, int user_id,
                               double amount,
                               const std::string& type);

bool db_update_raw_status(sqlite3* conn, int txn_id, const std::string& status);
inline bool db_update_raw_status(int txn_id, const std::string& status) {
  return db_update_raw_status(nullptr, txn_id, status);
}

std::string db_get_raw_status(sqlite3* conn, int txn_id);

int db_count_raw_by_status(sqlite3* conn, const std::string& status);
inline int db_count_raw_by_status(const std::string& status) {
  return db_count_raw_by_status(nullptr, status);
}

int db_count_raw_by_type(sqlite3* conn, const std::string& type);
inline int db_count_raw_by_type(const std::string& type) {
  return db_count_raw_by_type(nullptr, type);
}

int db_count_committed(sqlite3* conn);
inline int db_count_committed() {
  return db_count_committed(nullptr);
}

// Thread-safe balance retrieval using the global database handle.
double db_get_balance_global(int user_id);

#endif // DATABASE_H
