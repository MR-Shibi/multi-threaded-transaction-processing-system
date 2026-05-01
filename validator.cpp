// ============================================================
//  validator.cpp
//  VALIDATOR THREADS — Full Implementation
//
//  This is the most complex thread in the system.
//  It bridges all three IPC mechanisms:
//    Shared Memory (read) → SQLite (check) → Named Pipe (write)
// ============================================================
// ============================================================
//  validator.cpp — Updated with UI formatting and transition delay
// ============================================================

#include "validator.h"
#include "transaction.h"
#include "shared_buffer.h"
#include "fifo_queue.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <string>

extern std::atomic<bool> g_running;

// 1-second transition delay between pipeline stages
static const int TRANSITION_DELAY_US = 1000000;

// ── Build SQL commit query ────────────────────────────────────
static void build_commit_query(Transaction& txn) {
    double balance_change = txn.amount;
    if (strncmp(txn.transaction_type, "WITHDRAWAL", 10) == 0 ||
        strncmp(txn.transaction_type, "TRANSFER",   8)  == 0)
        balance_change = -txn.amount;

    double balance_after = txn.user_balance_at_time + balance_change;

    snprintf(txn.commit_query, MAX_QUERY_LEN,
        "INSERT INTO transactions"
        "(txn_id, user_id, amount, type, status, balance_after, committed_at) "
        "VALUES(%d, %d, %.2f, '%s', 'PAID', %.2f, %lld);"
        "UPDATE users SET balance = %.2f WHERE user_id = %d;",
        txn.transaction_id, txn.user_id, txn.amount,
        txn.transaction_type, balance_after,
        (long long)time(nullptr),
        balance_after, txn.user_id);

    txn.balance_after = balance_after;
}

// ── Reject a transaction ─────────────────────────────────────
static void reject_transaction(Transaction& txn,
                                const char*  reason,
                                int          thread_id) {
    strncpy(txn.validation_status, STATUS_REJECTED, MAX_STATUS_LEN - 1);
    strncpy(txn.rejection_reason,  reason,          MAX_REASON_LEN - 1);
    txn.validation_timestamp = time(nullptr);
    txn.validator_thread_id  = (long)pthread_self();
    db_update_raw_status(txn.transaction_id, "REJECTED");

    logger_log(ThreadType::VALIDATOR, thread_id,
        "TXN #" + std::to_string(txn.transaction_id)
        + " REJECTED | User:" + std::to_string(txn.user_id)
        + " | $" + std::to_string((int)txn.amount)
        + " | " + txn.transaction_type
        + " | " + reason);
}

// ============================================================
//  validator_thread()
// ============================================================
void* validator_thread(void* args) {
    ValidatorArgs* a        = static_cast<ValidatorArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int write_fd  = a->write_fd;
    int id        = a->thread_id;
    int validated = 0;
    int rejected  = 0;

    logger_log(ThreadType::VALIDATOR, id, "Started.");

    while (g_running.load() || shm_buffer_count(buf) > 0) {

        if (!g_running.load() && shm_buffer_count(buf) == 0) break;

        if (shm_buffer_count(buf) == 0) {
            usleep(10000);
            continue;
        }

        // ── Step 1: Consume from Shared Memory ───────────────
        Transaction txn = shm_buffer_consume(buf);

        logger_log(ThreadType::VALIDATOR, id,
            "TXN #" + std::to_string(txn.transaction_id)
            + " ◄── Shared Memory | User:"
            + std::to_string(txn.user_id)
            + " $" + std::to_string((int)txn.amount)
            + " " + txn.transaction_type);

        // ── Mark PROCESSING ───────────────────────────────────
        db_update_raw_status(txn.transaction_id, "PROCESSING");

        // ── Transition delay: "checking" stage ────────────────
        usleep(TRANSITION_DELAY_US);

        // ── Session check ─────────────────────────────────────
        bool session_ok = db_is_session_active(txn.user_id);
        if (!session_ok) {
            reject_transaction(txn,
                "No active session (logged out or expired)", id);
            rejected++;
            continue;
        }

        // ── Balance check ─────────────────────────────────────
        double balance = db_get_balance(txn.user_id);
        if (balance < 0) {
            reject_transaction(txn, "User not found in DB", id);
            rejected++;
            continue;
        }

        txn.user_balance_at_time = balance;

        if ((strncmp(txn.transaction_type, "WITHDRAWAL", 10) == 0 ||
             strncmp(txn.transaction_type, "TRANSFER",   8)  == 0) &&
             balance < txn.amount) {
            char reason[MAX_REASON_LEN];
            snprintf(reason, MAX_REASON_LEN,
                "Insufficient funds: balance $%.2f < requested $%.2f",
                balance, txn.amount);
            reject_transaction(txn, reason, id);
            rejected++;
            continue;
        }

        // ── Mark VALID ────────────────────────────────────────
        strncpy(txn.validation_status, STATUS_VALID, MAX_STATUS_LEN - 1);
        txn.validation_timestamp = time(nullptr);
        txn.validator_thread_id  = (long)pthread_self();
        txn.session_expiry       = 9999999999;

        // ── Build SQL query ───────────────────────────────────
        build_commit_query(txn);

        // ── Transition delay: Validator → Named Pipe ──────────
        usleep(TRANSITION_DELAY_US);

        // ── Write to Named Pipe ───────────────────────────────
        fifo_write_query(write_fd, txn.commit_query);

        // ── Mark DONE ─────────────────────────────────────────
        db_update_raw_status(txn.transaction_id, "DONE");

        validated++;

        logger_log(ThreadType::VALIDATOR, id,
            "TXN #" + std::to_string(txn.transaction_id)
            + " VALID ──► Named Pipe | Balance: $"
            + std::to_string((int)txn.user_balance_at_time)
            + " → $" + std::to_string((int)txn.balance_after));
    }

    logger_log(ThreadType::VALIDATOR, id,
        "Stopped. Validated: " + std::to_string(validated)
        + " | Rejected: " + std::to_string(rejected));

    return nullptr;
}