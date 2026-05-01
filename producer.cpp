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
// ============================================================
//  producer.cpp
//  PRODUCER THREADS
//
//  Automatic mode: SLOW / FAST / BURST with 1s transition delay
//  Manual mode:    Step-by-step guided wizard
//    Step 1 → Choose user (1-5, shown with name + balance + session)
//    Step 2 → Choose type (1=DEPOSIT  2=WITHDRAWAL  3=TRANSFER)
//    Step 3 → Enter amount (with balance limit shown)
//    Step 4 → Confirm summary before submitting
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
#include <cctype>    // toupper

extern std::atomic<bool> g_running;
extern std::atomic<int>  g_next_txn_id;

// 1-second transition delay between pipeline stages
static const int TRANSITION_DELAY_US = 1000000;

// ── User table (mirrors seeded DB data) ──────────────────────
static const struct {
    int         id;
    const char* name;
    bool        has_session;
} USERS[] = {
    {1, "Alice",   true},
    {2, "Bob",     true},
    {3, "Charlie", true},
    {4, "Diana",   true},
    {5, "Eve",     false},   // no session — will be rejected
};
static const int NUM_USERS = 5;

// ── Transaction types ─────────────────────────────────────────
static const char* TXN_TYPES[]  = {"DEPOSIT", "WITHDRAWAL", "TRANSFER"};
static const int   NUM_TYPES    = 3;

// ── Auto transaction types for random selection ───────────────
static const char* AUTO_TYPES[] = {"DEPOSIT", "WITHDRAWAL", "TRANSFER"};
static const int   NUM_AUTO     = 3;

// ============================================================
//  HELPER: make_transaction()
//  Used by the automatic producer threads.
// ============================================================
static Transaction make_transaction(int thread_id) {
    Transaction txn;
    txn.transaction_id = g_next_txn_id.fetch_add(1);
    txn.user_id        = USERS[rand() % NUM_USERS].id;
    txn.amount         = (double)((rand() % 19) + 1) * 50.0;
    strncpy(txn.transaction_type,
            AUTO_TYPES[rand() % NUM_AUTO], MAX_TYPE_LEN - 1);
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
//  HELPER: read_line()
//  Reads one line from stdin safely.
//  Returns false if stdin is closed (shutdown signal).
// ============================================================
static bool read_line(char* buf, int buf_size) {
    if (fgets(buf, buf_size, stdin) == nullptr) return false;
    buf[strcspn(buf, "\n")] = '\0';   // strip trailing newline
    // Strip leading/trailing spaces
    int start = 0;
    while (buf[start] == ' ') start++;
    if (start > 0) memmove(buf, buf + start, strlen(buf) - start + 1);
    int end = (int)strlen(buf) - 1;
    while (end >= 0 && buf[end] == ' ') buf[end--] = '\0';
    return true;
}

// ============================================================
//  HELPER: is_quit()
//  Returns true if the user typed quit / exit / q
// ============================================================
static bool is_quit(const char* line) {
    return (strcmp(line, "quit") == 0 ||
            strcmp(line, "exit") == 0 ||
            strcmp(line, "q")    == 0);
}

// ============================================================
//  producer_thread() — Automatic mode
// ============================================================
void* producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;
    ProducerStyle style     = a->style;

    const char* style_name = "UNKNOWN";
    int base_delay = 0;
    switch (style) {
        case ProducerStyle::SLOW:  style_name="SLOW";  base_delay=800000; break;
        case ProducerStyle::FAST:  style_name="FAST";  base_delay=200000; break;
        case ProducerStyle::BURST: style_name="BURST"; base_delay=50000;  break;
    }

    logger_log(ThreadType::PRODUCER, id,
               std::string("Started [") + style_name + " mode]"
               + " — 1s transition delay active.");

    while (g_running.load()) {

        if (style == ProducerStyle::BURST) {
            for (int b = 0; b < 4 && g_running.load(); b++) {
                Transaction txn = make_transaction(id);
                usleep(TRANSITION_DELAY_US);
                shm_buffer_produce(buf, txn);
                logger_log(ThreadType::PRODUCER, id,
                    "TXN #" + std::to_string(txn.transaction_id)
                    + " ──► Shared Memory  [slots: "
                    + std::to_string(shm_buffer_count(buf))
                    + "/" + std::to_string(SHARED_BUFFER_SIZE) + "]");
                usleep(base_delay);
            }
            usleep(2000000);
        } else {
            Transaction txn = make_transaction(id);
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
//  manual_producer_thread() — Step-by-step wizard
//
//  WIZARD FLOW:
//    ┌────────────────────────────────────────┐
//    │ STEP 1: Show user list                 │
//    │         User types 1-5                 │
//    │         If user 5 → warn, ask confirm  │
//    ├────────────────────────────────────────┤
//    │ STEP 2: Show transaction type menu     │
//    │         User types 1, 2, or 3          │
//    ├────────────────────────────────────────┤
//    │ STEP 3: Show amount panel              │
//    │         Shows current balance + limit  │
//    │         User types a number            │
//    ├────────────────────────────────────────┤
//    │ STEP 4: Confirm summary panel          │
//    │         Enter=confirm  c=cancel        │
//    ├────────────────────────────────────────┤
//    │ SUCCESS: Queued panel shown            │
//    │ Ask: another transaction or quit?      │
//    └────────────────────────────────────────┘
// ============================================================
void* manual_producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;

    logger_log(ThreadType::PRODUCER, id,
               "Manual wizard thread started.");

    char line[128];

    while (g_running.load()) {

        // ════════════════════════════════════════════
        //  STEP 1: SELECT USER
        // ════════════════════════════════════════════
        ui_wizard_show_users();
        ui_wizard_prompt("Select user [1-5] or 'q' to quit: ");

        if (!read_line(line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id,
                       "Manual input stopped by user.");
            break;
        }

        // Parse user selection
        int user_choice = atoi(line);
        if (user_choice < 1 || user_choice > 5) {
            ui_wizard_error("Please enter a number between 1 and 5.");
            continue;
        }

        int         user_id   = USERS[user_choice - 1].id;
        const char* user_name = USERS[user_choice - 1].name;
        bool        has_sess  = USERS[user_choice - 1].has_session;

        // ── Warn if user 5 (no session) ──────────────
        if (!has_sess) {
            ui_wizard_warn_no_session(user_name);
            ui_wizard_prompt("Continue anyway? [y/n]: ");
            if (!read_line(line, sizeof(line))) break;
            if (line[0] != 'y' && line[0] != 'Y') {
                ui_wizard_show_cancelled();
                continue;   // restart wizard
            }
        }

        // ════════════════════════════════════════════
        //  STEP 2: SELECT TRANSACTION TYPE
        // ════════════════════════════════════════════
        ui_wizard_show_types(user_id, user_name);
        ui_wizard_prompt("Select type [1-3] or 'q' to quit: ");

        if (!read_line(line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id,
                       "Manual input stopped by user.");
            break;
        }

        int type_choice = atoi(line);
        if (type_choice < 1 || type_choice > 3) {
            ui_wizard_error("Please enter 1, 2, or 3.");
            continue;
        }

        const char* txn_type = TXN_TYPES[type_choice - 1];

        // ════════════════════════════════════════════
        //  STEP 3: ENTER AMOUNT
        // ════════════════════════════════════════════

        // Fetch current balance from DB for display
        double balance = db_get_balance(user_id);
        if (balance < 0) balance = 0.0;  // stub fallback

        ui_wizard_show_amount(user_id, user_name, txn_type, balance);
        ui_wizard_prompt("Enter amount or 'q' to quit: ");

        if (!read_line(line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id,
                       "Manual input stopped by user.");
            break;
        }

        double amount = atof(line);

        // ── Validate amount ───────────────────────────
        if (amount <= 0) {
            ui_wizard_error("Amount must be greater than zero.");
            continue;
        }
        if (amount > 10000) {
            ui_wizard_error("Maximum amount per transaction is $10,000.");
            continue;
        }

        // Soft warning for overdraft (validator will reject anyway)
        if ((strcmp(txn_type, "WITHDRAWAL") == 0 ||
             strcmp(txn_type, "TRANSFER")   == 0) &&
             amount > balance && balance > 0) {
            ui_wizard_error(
                "Amount exceeds balance. "
                "Validator WILL REJECT this transaction.");
            ui_wizard_prompt("Submit anyway? [y/n]: ");
            if (!read_line(line, sizeof(line))) break;
            if (line[0] != 'y' && line[0] != 'Y') {
                ui_wizard_show_cancelled();
                continue;
            }
        }

        // ════════════════════════════════════════════
        //  STEP 4: CONFIRM
        // ════════════════════════════════════════════
        ui_wizard_show_confirm(user_id, user_name, txn_type, amount);
        ui_wizard_prompt("Press Enter to CONFIRM or 'c' to cancel: ");

        if (!read_line(line, sizeof(line))) break;

        if (line[0] == 'c' || line[0] == 'C') {
            ui_wizard_show_cancelled();
            continue;
        }

        // ════════════════════════════════════════════
        //  BUILD + SUBMIT TRANSACTION
        // ════════════════════════════════════════════
        Transaction txn;
        txn.transaction_id = g_next_txn_id.fetch_add(1);
        txn.user_id        = user_id;
        txn.amount         = amount;
        strncpy(txn.transaction_type, txn_type, MAX_TYPE_LEN - 1);
        txn.timestamp      = time(nullptr);
        txn.retry_count    = 0;

        // Insert audit record
        db_insert_raw_transaction(txn.transaction_id,
                                  txn.user_id,
                                  txn.amount,
                                  std::string(txn.transaction_type));

        // 1-second transition delay (visible pipeline stage change)
        usleep(TRANSITION_DELAY_US);

        // Push to shared memory buffer
        shm_buffer_produce(buf, txn);

        // ── Success panel ──────────────────────────────
        ui_wizard_show_queued(txn.transaction_id,
                              user_id, user_name,
                              txn_type, amount);

        logger_log(ThreadType::PRODUCER, id,
            "MANUAL TXN #" + std::to_string(txn.transaction_id)
            + " | User:" + std::to_string(user_id)
            + " [" + std::string(user_name) + "]"
            + " | $" + std::to_string((int)amount)
            + " | " + txn_type
            + " ──► Shared Memory");

        // ════════════════════════════════════════════
        //  ASK: ANOTHER TRANSACTION OR QUIT?
        // ════════════════════════════════════════════
        ui_wizard_ask_another();

        if (!read_line(line, sizeof(line))) break;

        if (is_quit(line) ||
            line[0] == 'q' || line[0] == 'Q' ||
            line[0] == '0') {
            logger_log(ThreadType::PRODUCER, id,
                       "Manual input stopped by user.");
            break;
        }
        // If they typed 1 or Enter or anything else → loop again
    }

    logger_log(ThreadType::PRODUCER, id,
               "Manual producer thread exiting.");
    return nullptr;
}