// ============================================================
//  producer.cpp
//  PRODUCER THREADS — Full Implementation
//
//  Automatic producers generate random transactions and feed
//  them into the shared memory buffer.
//  The manual producer reads from the keyboard.
// ============================================================
// ============================================================
//  producer.cpp — Updated with rich UI and 1s transition delay
// ============================================================

#include "producer.h"
#include "transaction.h"
#include "shared_buffer.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <string>

extern std::atomic<bool> g_running;
extern std::atomic<int>  g_next_txn_id;

static const char* TXN_TYPES[] = {"DEPOSIT", "WITHDRAWAL", "TRANSFER"};
static const int   NUM_TYPES   = 3;
static const int   VALID_USERS[] = {1, 2, 3, 4, 5};
static const int   NUM_USERS     = 5;

// Transition delay between pipeline stages (1 second = 1,000,000 us)
static const int TRANSITION_DELAY_US = 1000000;

// ── Build and log a new transaction ──────────────────────────
static Transaction make_transaction(int thread_id) {
    Transaction txn;
    txn.transaction_id = g_next_txn_id.fetch_add(1);
    txn.user_id        = VALID_USERS[rand() % NUM_USERS];
    txn.amount         = (double)((rand() % 19) + 1) * 50.0;
    strncpy(txn.transaction_type,
            TXN_TYPES[rand() % NUM_TYPES], MAX_TYPE_LEN - 1);
    txn.timestamp   = time(nullptr);
    txn.retry_count = 0;

    db_insert_raw_transaction(txn.transaction_id, txn.user_id,
                              txn.amount,
                              std::string(txn.transaction_type));

    logger_log(ThreadType::PRODUCER, thread_id,
        "Created TXN #" + std::to_string(txn.transaction_id)
        + " | User:" + std::to_string(txn.user_id)
        + " | $" + std::to_string((int)txn.amount)
        + " | " + txn.transaction_type);

    return txn;
}

// ============================================================
//  producer_thread() — Automatic mode with transition delay
// ============================================================
void* producer_thread(void* args) {
    ProducerArgs* a   = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id    = a->thread_id;
    ProducerStyle style = a->style;

    const char* style_name = "UNKNOWN";
    int base_delay = 0;
    switch (style) {
        case ProducerStyle::SLOW:  style_name = "SLOW";  base_delay = 800000; break;
        case ProducerStyle::FAST:  style_name = "FAST";  base_delay = 200000; break;
        case ProducerStyle::BURST: style_name = "BURST"; base_delay = 50000;  break;
    }

    logger_log(ThreadType::PRODUCER, id,
               std::string("Started [") + style_name + " mode] "
               + "— 1s transition delay active.");

    while (g_running.load()) {

        if (style == ProducerStyle::BURST) {
            for (int b = 0; b < 4 && g_running.load(); b++) {
                Transaction txn = make_transaction(id);

                // ── Transition delay: Producer → Shared Memory ──
                // Shows data moving visibly between stages
                usleep(TRANSITION_DELAY_US);

                shm_buffer_produce(buf, txn);

                logger_log(ThreadType::PRODUCER, id,
                    "TXN #" + std::to_string(txn.transaction_id)
                    + " ──► Shared Memory  [slots: "
                    + std::to_string(shm_buffer_count(buf))
                    + "/" + std::to_string(SHARED_BUFFER_SIZE) + "]");

                usleep(base_delay);
            }
            usleep(2000000);  // 2s burst pause

        } else {
            Transaction txn = make_transaction(id);

            // ── Transition delay: Producer → Shared Memory ──
            usleep(TRANSITION_DELAY_US);

            shm_buffer_produce(buf, txn);

            logger_log(ThreadType::PRODUCER, id,
                "TXN #" + std::to_string(txn.transaction_id)
                + " ──► Shared Memory  [slots: "
                + std::to_string(shm_buffer_count(buf))
                + "/" + std::to_string(SHARED_BUFFER_SIZE) + "]");

            usleep(base_delay);
        }
    }

    logger_log(ThreadType::PRODUCER, id, "Stopped.");
    return nullptr;
}

// ============================================================
//  manual_producer_thread() — Redesigned input UI
// ============================================================
void* manual_producer_thread(void* args) {
    ProducerArgs* a   = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id = a->thread_id;

    // Show the styled input panel
    ui_print_input_panel();

    logger_log(ThreadType::PRODUCER, id,
               "Manual input thread ready.");

    char line[256];

    while (g_running.load()) {

        // Show styled prompt
        ui_print_input_prompt();

        if (fgets(line, sizeof(line), stdin) == nullptr) break;

        // Strip newline
        line[strcspn(line, "\n")] = '\0';

        // Empty input — just re-prompt
        if (strlen(line) == 0) continue;

        // Quit command
        if (strncmp(line, "quit", 4) == 0 ||
            strncmp(line, "exit", 4) == 0 ||
            strncmp(line, "q",    1) == 0) {
            logger_log(ThreadType::PRODUCER, id,
                       "Manual input stopped by user.");
            break;
        }

        // Help command
        if (strncmp(line, "help", 4) == 0 ||
            strncmp(line, "?",    1) == 0) {
            ui_print_input_panel();
            continue;
        }

        // ── Parse ─────────────────────────────────────────────
        int    user_id = 0;
        double amount  = 0.0;
        char   type[MAX_TYPE_LEN] = {0};

        int parsed = sscanf(line, "%d %lf %15s",
                            &user_id, &amount, type);

        // ── Validate field count ──────────────────────────────
        if (parsed < 3) {
            ui_print_input_error("FORMAT",
                "Expected: <user_id>  <amount>  <type>");
            ui_print_input_hint(
                "Example:  3  250  WITHDRAWAL");
            continue;
        }

        // ── Validate user_id ──────────────────────────────────
        if (user_id < 1 || user_id > 5) {
            ui_print_input_error("user_id",
                "Must be 1–5  (1=Alice 2=Bob 3=Charlie 4=Diana 5=Eve)");
            continue;
        }

        // ── Validate amount ───────────────────────────────────
        if (amount <= 0) {
            ui_print_input_error("amount",
                "Must be a positive number  e.g. 250");
            continue;
        }
        if (amount > 10000) {
            ui_print_input_error("amount",
                "Maximum single transaction is $10,000");
            continue;
        }

        // ── Validate type ─────────────────────────────────────
        bool type_ok =
            strncmp(type, "DEPOSIT",    7)  == 0 ||
            strncmp(type, "WITHDRAWAL", 10) == 0 ||
            strncmp(type, "TRANSFER",   8)  == 0;

        if (!type_ok) {
            ui_print_input_error("type",
                "Must be DEPOSIT, WITHDRAWAL, or TRANSFER");
            continue;
        }

        // ── Warn about user 5 (no session) ────────────────────
        if (user_id == 5) {
            ui_print_input_hint(
                "User 5 (Eve) has no active session — "
                "this transaction will be REJECTED by the validator.");
        }

        // ── Build transaction ─────────────────────────────────
        Transaction txn;
        txn.transaction_id = g_next_txn_id.fetch_add(1);
        txn.user_id        = user_id;
        txn.amount         = amount;
        strncpy(txn.transaction_type, type, MAX_TYPE_LEN - 1);
        txn.timestamp   = time(nullptr);
        txn.retry_count = 0;

        db_insert_raw_transaction(txn.transaction_id,
                                  txn.user_id, txn.amount,
                                  std::string(txn.transaction_type));

        // ── Transition delay ──────────────────────────────────
        usleep(TRANSITION_DELAY_US);

        shm_buffer_produce(buf, txn);

        // ── Success feedback ──────────────────────────────────
        ui_print_input_success(txn.transaction_id,
                               txn.user_id,
                               txn.amount,
                               txn.transaction_type);

        logger_log(ThreadType::PRODUCER, id,
            "MANUAL TXN #" + std::to_string(txn.transaction_id)
            + " | User:" + std::to_string(txn.user_id)
            + " | $" + std::to_string((int)txn.amount)
            + " | " + txn.transaction_type
            + " ──► Shared Memory");
    }

    logger_log(ThreadType::PRODUCER, id,
               "Manual producer thread exiting.");
    return nullptr;
}