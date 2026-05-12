#include "validator.h"
#include "Transaction.h"
#include "database.h"
#include "fifo_queue.h"
#include "logger.h"
#include "shared_buffer.h"
#include "ui.h"

#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <csignal>

extern volatile sig_atomic_t g_running;


static const int TRANSITION_DELAY_US = 100000;

// Constructs an atomic SQL query based on transaction type to be executed by the updater thread.
static void build_commit_query(Transaction &txn) {
  long long now = (long long)time(nullptr);

  if (strncmp(txn.transaction_type, "DEPOSIT", 7) == 0) {
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

// Marks a transaction as rejected in the database and logs the failure.
static void reject_transaction(Transaction &txn, const char *reason,
                               int thread_id) {
  strncpy(txn.validation_status, STATUS_REJECTED, MAX_STATUS_LEN - 1);
  strncpy(txn.rejection_reason, reason, MAX_REASON_LEN - 1);
  txn.validation_timestamp = time(nullptr);
  txn.validator_thread_id = (long)pthread_self();
  db_update_raw_status(txn.transaction_id, "REJECTED");

  logger_log(ThreadType::VALIDATOR, thread_id,
             "Transaction #" + std::to_string(txn.transaction_id) +
                 " REJECTED  |  User:" + std::to_string(txn.user_id) +
                 "  |  $" + std::to_string((int)txn.amount) + " " +
                 txn.transaction_type + "  |  Reason: " + reason);
}

// Entry point for validator threads; synchronizes with producers via shared memory semaphores.
void *validator_thread(void *args) {
  ValidatorArgs *a = static_cast<ValidatorArgs *>(args);
  SharedMemoryBuffer *buf = a->buffer;
  int write_fd = a->write_fd;
  int id = a->thread_id;
  int validated = 0;
  int rejected = 0;

  std::this_thread::sleep_for(std::chrono::milliseconds(300 + (rand() % 400)));
  sqlite3 *conn = db_open_connection();
  if (!conn) {
    logger_log(ThreadType::VALIDATOR, id,
               "FATAL: could not open DB connection. Thread exiting.");
    return nullptr;
  }

  logger_log(ThreadType::VALIDATOR, id,
             "Validator is ready and waiting for transactions.");
  ui_set_thread_status("VALIDATOR", id, "Idle - waiting");

  while (true) {
    Transaction txn = shm_buffer_consume(buf);
    ui_queue_pop();

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
    if (g_running)
      usleep(TRANSITION_DELAY_US);

    bool session_ok = db_is_session_active(conn, txn.user_id);
    if (!session_ok) {
      std::string log = "Auditing Transaction #" +
                        std::to_string(txn.transaction_id) + " (" +
                        std::string(txn.transaction_type) + ")\n" +
                        "                                  +-- [FAIL] Session "
                        "Verification (Not logged in)";
      logger_log(ThreadType::VALIDATOR, id, log);
      reject_transaction(txn, "Account is not logged in", id);
      rejected++;
      continue;
    }

    double balance = db_get_balance(conn, txn.user_id);
    if (balance < 0) {
      std::string log =
          "Auditing Transaction #" + std::to_string(txn.transaction_id) + " (" +
          std::string(txn.transaction_type) + ")\n" +
          "                                  |-- [PASS] Session Verification\n" +
          "                                  +-- [FAIL] Account not found in "
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
          "                                  |-- [PASS] Session Verification\n" +
          "                                  +-- [FAIL] Balance Pre-check (Too "
          "low)";
      logger_log(ThreadType::VALIDATOR, id, log);

      reject_transaction(txn, reason, id);
      rejected++;
      continue;
    }

    strncpy(txn.validation_status, STATUS_VALID, MAX_STATUS_LEN - 1);
    txn.validation_timestamp = time(nullptr);
    txn.validator_thread_id = (long)pthread_self();
    txn.session_expiry = 9999999999;

    build_commit_query(txn);

    if (g_running)
      usleep(TRANSITION_DELAY_US);

    fifo_write_query(write_fd, txn.commit_query);
    db_update_raw_status(txn.transaction_id, "FORWARDED");

    validated++;

    std::string log =
        "Auditing Transaction #" + std::to_string(txn.transaction_id) + " (" +
        std::string(txn.transaction_type) + ")\n" +
        "                                  |-- [PASS] Session Verification\n" +
        "                                  |-- [PASS] Balance Pre-check ($" +
        std::to_string((int)balance) + " >= $" +
        std::to_string((int)txn.amount) + ")\n" +
        "                                  +-- [ACCEPTED] Generated atomic SQL. "
        "Forwarded to Updater.";
    logger_log(ThreadType::VALIDATOR, id, log);
    char ok_st[72];
    snprintf(ok_st, 72, "Forwarded #%d  ok:%d  rej:%d", txn.transaction_id,
             validated, rejected);
    ui_set_thread_status("VALIDATOR", id, ok_st);
  }

  db_close_connection(conn);

  logger_log(ThreadType::VALIDATOR, id,
             "Validator finished  |  " + std::to_string(validated) +
                 " accepted  |  " + std::to_string(rejected) + " rejected");

  return nullptr;
}