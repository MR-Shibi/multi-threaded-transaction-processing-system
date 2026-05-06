#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <ctime>
#include <cstring>

static const int MAX_TYPE_LEN    = 16;
static const int MAX_STATUS_LEN  = 16;
static const int MAX_REASON_LEN  = 128;
static const int MAX_QUERY_LEN   = 512;
static const int MAX_NAME_LEN    = 32;

static constexpr const char* TYPE_DEPOSIT    = "DEPOSIT";
static constexpr const char* TYPE_WITHDRAWAL = "WITHDRAWAL";
static constexpr const char* TYPE_TRANSFER   = "TRANSFER";

static constexpr const char* STATUS_PENDING    = "PENDING";
static constexpr const char* STATUS_PROCESSING = "PROCESSING";
static constexpr const char* STATUS_VALID      = "VALID";
static constexpr const char* STATUS_REJECTED   = "REJECTED";
static constexpr const char* STATUS_PAID       = "PAID";
static constexpr const char* STATUS_FAILED     = "FAILED";

struct Transaction {
    // ── Producer fields ──────────────────────────────────────
    int    transaction_id;
    int    user_id;
    double amount;
    char   transaction_type[MAX_TYPE_LEN];
    time_t timestamp;
    int    retry_count;
    // For TRANSFER: recipient (0 = not a transfer or auto-generated)
    int    recipient_id;
    char   recipient_name[MAX_NAME_LEN];

    // ── Validator fields ─────────────────────────────────────
    char   validation_status[MAX_STATUS_LEN];
    char   rejection_reason[MAX_REASON_LEN];
    double user_balance_at_time;
    time_t session_expiry;
    long   validator_thread_id;
    time_t validation_timestamp;
    char   commit_query[MAX_QUERY_LEN];

    // ── DB Updater fields ────────────────────────────────────
    char   final_status[MAX_STATUS_LEN];
    double balance_after;
    time_t commit_timestamp;
    long   updater_thread_id;

    // ── Poison Pill ──────────────────────────────────────────
    // When true, this is a shutdown signal — not a real transaction.
    // Consumers (validators) must exit their loop immediately upon
    // receiving this instead of trying to process it.
    bool is_shutdown;

    Transaction() { memset(this, 0, sizeof(Transaction)); }

    // Factory: creates a poison-pill transaction for graceful shutdown.
    // main() produces one of these per validator thread.
    static Transaction make_shutdown_pill() {
        Transaction t;
        t.is_shutdown = true;
        return t;
    }
};

#endif // TRANSACTION_H