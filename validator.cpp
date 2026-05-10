// ============================================================
//  validator.cpp — Updated with human-readable log messages
// ============================================================

#include "validator.h"
#include "Transaction.h"
#include "database.h"
#include "fifo_queue.h"
#include "logger.h"
#include "shared_buffer.h"
#include "ui.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>

extern std::atomic<bool> g_running;

// NOTE: g_running is no longer polled here. Shutdown is driven by
// the Poison Pill pattern — main() pushes a shutdown transaction
// into the shared buffer to wake and stop each validator cleanly.

static const int TRANSITION_DELAY_US = 100000; // 100ms between pipeline stages

// ── Build SQL commit query ────────────────────────────────────
//
//  TOCTOU FIX: The old approach computed an absolute balance:
//    balance_after = balance_now - amount
//    SQL: UPDATE balance = {absolute_value}
//
//  This creates a race: two validators can both read balance=$100,
//  both compute $100-$100=$0, both issue UPDATE balance=0 — and
//  the user effectively withdraws $200 for free.
//
//  The fix: use RELATIVE arithmetic in SQL so the database engine
//  serializes the update atomically:
//
//    DEPOSIT:
//      UPDATE balance = balance + X              (always safe)
//
//    WITHDRAWAL/TRANSFER:
//      UPDATE balance = balance - X WHERE balance >= X
//      INSERT ... SELECT ... WHERE changes() > 0
//
//  The WHERE balance >= X guard means: if a concurrent transaction
//  already consumed the funds, this UPDATE changes 0 rows.
//  The "changes() > 0" on the INSERT ensures the audit record is
//  only created if the UPDATE actually succeeded.
//  All of this executes inside one BEGIN/COMMIT in the Updater,
//  so it is fully atomic.
static void build_commit_query(Transaction &txn) {
  long long now = (long long)time(nullptr);

  if (strncmp(txn.transaction_type, "DEPOSIT", 7) == 0) {
    // DEPOSIT: unconditional relative increase
    snprintf(txn.commit_query, MAX_QUERY_LEN,
             "UPDATE users SET balance = balance + %.2f "
             "WHERE user_id = %d; "
             "INSERT INTO transactions"
             "(txn_id,user_id,amount,type,status,balance_after,committed_at) "
             "SELECT %d,%d,%.2f,'%s','PAID',balance,%lld "
             "FROM users WHERE user_id = %d;",
             txn.amount, txn.user_id, txn.transaction_id, txn.user_id,
             txn.amount, txn.transaction_type, now, txn.user_id);
    txn.balance_after = txn.user_balance_at_time + txn.amount;

  } else if (strncmp(txn.transaction_type, "TRANSFER", 8) == 0) {
    // TRANSFER: debit sender (atomic guard), then credit recipient ONLY if
    // debit succeeded, then audit. changes() > 0 ensures the next statement
    // only executes if the previous statement updated a row.
    snprintf(txn.commit_query, MAX_QUERY_LEN,
             "UPDATE users SET balance = balance - %.2f "
             "WHERE user_id = %d AND balance >= %.2f; "
             "UPDATE users SET balance = balance + %.2f "
             "WHERE user_id = %d AND changes() > 0; "
             "INSERT INTO transactions"
             "(txn_id,user_id,amount,type,status,balance_after,committed_at) "
             "SELECT %d,%d,%.2f,'%s','PAID',balance,%lld "
             "FROM users WHERE user_id = %d AND changes() > 0;",
             txn.amount, txn.user_id, txn.amount, txn.amount, txn.recipient_id,
             txn.transaction_id, txn.user_id, txn.amount, txn.transaction_type,
             now, txn.user_id);
    txn.balance_after = txn.user_balance_at_time - txn.amount;

  } else {
    // WITHDRAWAL: debit sender (atomic guard), then insert audit.
    snprintf(txn.commit_query, MAX_QUERY_LEN,
             "UPDATE users SET balance = balance - %.2f "
             "WHERE user_id = %d AND balance >= %.2f; "
             "INSERT INTO transactions"
             "(txn_id,user_id,amount,type,status,balance_after,committed_at) "
             "SELECT %d,%d,%.2f,'%s','PAID',balance,%lld "
             "FROM users WHERE user_id = %d AND changes() > 0;",
             txn.amount, txn.user_id, txn.amount, txn.transaction_id,
             txn.user_id, txn.amount, txn.transaction_type, now, txn.user_id);
    txn.balance_after = txn.user_balance_at_time - txn.amount;
  }
}

// ── Reject a transaction ─────────────────────────────────────
// db_update_raw_status uses Tier 1 (global connection + mutex) — no conn
// needed.
static void reject_transaction(Transaction &txn, const char *reason,
                               int thread_id) {
  strncpy(txn.validation_status, STATUS_REJECTED, MAX_STATUS_LEN - 1);
  strncpy(txn.rejection_reason, reason, MAX_REASON_LEN - 1);
  txn.validation_timestamp = time(nullptr);
  txn.validator_thread_id = (long)pthread_self();
  db_update_raw_status(txn.transaction_id, "REJECTED"); // Tier 1 call

  logger_log(ThreadType::VALIDATOR, thread_id,
             "Transaction #" + std::to_string(txn.transaction_id) +
                 " REJECTED  |  User:" + std::to_string(txn.user_id) +
                 "  |  $" + std::to_string((int)txn.amount) + " " +
                 txn.transaction_type + "  |  Reason: " + reason);
}

// ============================================================
//  validator_thread()
//
//  SHUTDOWN: The old approach polled shm_buffer_count() then called
//  shm_buffer_consume(). This is NOT atomic — another validator could
//  consume the last item between the check and the consume, causing
//  this thread to block forever on sem_wait() and deadlock main().
//
//  FIX: Poison Pill pattern.
//    - We ALWAYS block on shm_buffer_consume() with no pre-check.
//    - When main() wants to stop, it pushes Transaction::make_shutdown_pill()
//      into the buffer — one pill per validator thread.
//    - This guarantees every blocked validator is woken up exactly once
//      and exits cleanly. No polling, no race, no deadlock.
// ============================================================
void *validator_thread(void *args) {
  ValidatorArgs *a = static_cast<ValidatorArgs *>(args);
  SharedMemoryBuffer *buf = a->buffer;
  int write_fd = a->write_fd;
  int id = a->thread_id;
  int validated = 0;
  int rejected = 0;

  // ── Open per-thread DB connection (Tier 2) ───────────────
  // This connection is exclusively owned by this thread.
  // Simulate detailed audit time (checking session, balance, etc.)
  std::this_thread::sleep_for(std::chrono::milliseconds(300 + (rand() % 400)));
  sqlite3 *conn = db_open_connection();
  if (!conn) {
    logger_log(ThreadType::VALIDATOR, id,
               "FATAL: could not open DB connection. Thread exiting.");
    return nullptr;
  }

  logger_log(ThreadType::VALIDATOR, id,
             "Validator is ready and waiting for transactions.");
  ui_set_thread_status("VALIDATOR", id, "Idle — waiting");

  while (true) {
    // Always block here — no racy pre-check on count.
    // We will unblock when either a real transaction or a poison
    // pill is pushed into the buffer.
    Transaction txn = shm_buffer_consume(buf);
    ui_queue_pop(); // Live UI update for the buffer bar

    // ── Poison pill check: exit immediately ───────────────
    if (txn.is_shutdown) {
      logger_log(ThreadType::VALIDATOR, id,
                 "Received shutdown signal. Stopping.");
      break;
    }

    db_update_raw_status(txn.transaction_id, "PROCESSING");
    char val_st[72];
    snprintf(val_st, 72, "Auditing #%d (%s)", txn.transaction_id,
             txn.transaction_type);
    ui_set_thread_status("VALIDATOR", id, val_st);
    if (g_running.load())
      usleep(TRANSITION_DELAY_US);

    // ── Session check (Tier 2 — no mutex) ────────────────
    bool session_ok = db_is_session_active(conn, txn.user_id);
    if (!session_ok) {
      std::string log = "Auditing Transaction #" +
                        std::to_string(txn.transaction_id) + " (" +
                        std::string(txn.transaction_type) + ")\n" +
                        "                                  └─ [FAIL] Session "
                        "Verification (Not logged in)";
      logger_log(ThreadType::VALIDATOR, id, log);
      reject_transaction(txn, "Account is not logged in", id);
      rejected++;
      continue;
    }

    // ── Balance check (Tier 2 — no mutex) ────────────────
    double balance = db_get_balance(conn, txn.user_id);
    if (balance < 0) {
      std::string log =
          "Auditing Transaction #" + std::to_string(txn.transaction_id) + " (" +
          std::string(txn.transaction_type) + ")\n" +
          "                                  ├─ [PASS] Session Verification\n" +
          "                                  └─ [FAIL] Account not found in "
          "database";
      logger_log(ThreadType::VALIDATOR, id, log);
      reject_transaction(txn, "Account not found in database", id);
      rejected++;
      continue;
    }

    txn.user_balance_at_time = balance;

    if ((strncmp(txn.transaction_type, "WITHDRAWAL", 10) == 0 ||
         strncmp(txn.transaction_type, "TRANSFER", 8) == 0) &&
        balance < txn.amount) {
      char reason[MAX_REASON_LEN];
      snprintf(reason, MAX_REASON_LEN,
               "Not enough funds  (balance $%.2f  requested $%.2f)", balance,
               txn.amount);

      std::string log =
          "Auditing Transaction #" + std::to_string(txn.transaction_id) + " (" +
          std::string(txn.transaction_type) + ")\n" +
          "                                  ├─ [PASS] Session Verification\n" +
          "                                  └─ [FAIL] Balance Pre-check (Too "
          "low)";
      logger_log(ThreadType::VALIDATOR, id, log);

      reject_transaction(txn, reason, id);
      rejected++;
      continue;
    }

    // ── Mark VALID ────────────────────────────────────────
    strncpy(txn.validation_status, STATUS_VALID, MAX_STATUS_LEN - 1);
    txn.validation_timestamp = time(nullptr);
    txn.validator_thread_id = (long)pthread_self();
    txn.session_expiry = 9999999999;

    // ── Build SQL query ───────────────────────────────────
    build_commit_query(txn);

    // Delay — makes pipe-write stage visible in the terminal
    if (g_running.load())
      usleep(TRANSITION_DELAY_US);

    // ── Write to Named Pipe ───────────────────────────────
    fifo_write_query(write_fd, txn.commit_query);
    db_update_raw_status(txn.transaction_id, "FORWARDED");

    validated++;

    std::string log =
        "Auditing Transaction #" + std::to_string(txn.transaction_id) + " (" +
        std::string(txn.transaction_type) + ")\n" +
        "                                  ├─ [PASS] Session Verification\n" +
        "                                  ├─ [PASS] Balance Pre-check ($" +
        std::to_string((int)balance) + " >= $" +
        std::to_string((int)txn.amount) + ")\n" +
        "                                  └─ [ACCEPTED] Generated atomic SQL. "
        "Forwarded to Updater.";
    logger_log(ThreadType::VALIDATOR, id, log);
    char ok_st[72];
    snprintf(ok_st, 72, "Forwarded #%d  ok:%d  rej:%d", txn.transaction_id,
             validated, rejected);
    ui_set_thread_status("VALIDATOR", id, ok_st);
  }

  // ── Close per-thread connection ───────────────────────────
  db_close_connection(conn);

  logger_log(ThreadType::VALIDATOR, id,
             "Validator finished  |  " + std::to_string(validated) +
                 " accepted  |  " + std::to_string(rejected) + " rejected");

  return nullptr;
}