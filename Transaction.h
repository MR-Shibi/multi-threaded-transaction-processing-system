#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <ctime>
#include <cstring>

static const int MAX_TYPE_LEN    = 16;
static const int MAX_STATUS_LEN  = 16;
static const int MAX_REASON_LEN  = 128;
static const int MAX_QUERY_LEN   = 512;

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

    // ── Validator fields ─────────────────────────────────────
    char   validation_status[MAX_STATUS_LEN];
    char   rejection_reason[MAX_REASON_LEN];
    double user_balance_at_time;
    time_t session_expiry;
    long   validator_thread_id;
    time_t validation_timestamp;
    char   commit_query[MAX_QUERY_LEN];  // Pre-built SQL INSERT — DB Updater runs this

    // ── DB Updater fields ────────────────────────────────────
    char   final_status[MAX_STATUS_LEN];
    double balance_after;
    time_t commit_timestamp;
    long   updater_thread_id;

    // Zero-initialize on construction — critical for shared memory safety
    Transaction() { memset(this, 0, sizeof(Transaction)); }
};

#endif // TRANSACTION_H