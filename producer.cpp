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
// ============================================================
//  producer.cpp
//
//  Manual wizard sets g_input_active = true before every
//  prompt and false after reading the line. This tells the
//  monitor to skip printing while the user is typing.
//
//  The wizard also completes ONE full transaction before
//  looping — it never moves to Step 2 while still on Step 1.
// ============================================================
// ============================================================
//  producer.cpp
//
//  Manual wizard sets g_input_active = true before every
//  prompt and false after reading the line. This tells the
//  monitor to skip printing while the user is typing.
//
//  The wizard also completes ONE full transaction before
//  looping — it never moves to Step 2 while still on Step 1.
// ============================================================

#include "producer.h"
#include "transaction.h"
#include "shared_buffer.h"
#include "database.h"
#include "logger.h"
#include "ui.h"
#include "monitor.h"   // for g_input_active

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <string>

extern std::atomic<bool> g_running;
extern std::atomic<int>  g_next_txn_id;

static const int TRANSITION_DELAY_US = 1000000;

static const char* TXN_TYPES[] = {"DEPOSIT", "WITHDRAWAL", "TRANSFER"};

static const struct {
    int         id;
    const char* name;
    bool        has_session;
} USERS[] = {
    {1, "Alice",   true},
    {2, "Bob",     true},
    {3, "Charlie", true},
    {4, "Diana",   true},
    {5, "Eve",     false},
};
static const int NUM_USERS = 5;

// ── Automatic transaction helper ──────────────────────────────
static Transaction make_transaction(int thread_id) {
    static const char* AUTO_TYPES[] = {"DEPOSIT","WITHDRAWAL","TRANSFER"};
    Transaction txn;
    txn.transaction_id = g_next_txn_id.fetch_add(1);
    txn.user_id        = USERS[rand() % NUM_USERS].id;
    txn.amount         = (double)((rand() % 19) + 1) * 50.0;
    strncpy(txn.transaction_type,
            AUTO_TYPES[rand() % 3], MAX_TYPE_LEN - 1);
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
//  HELPER: wizard_read_line()
//
//  Sets g_input_active = true BEFORE printing the prompt
//  so the monitor knows to stay silent.
//  Sets g_input_active = false AFTER the user presses Enter
//  so the monitor can resume printing.
//
//  Returns false if stdin is closed (shutdown).
// ============================================================
static bool wizard_read_line(const char* prompt,
                              char* buf, int buf_size) {
    // Signal monitor to pause
    g_input_active.store(true);

    // Print the prompt
    ui_wizard_prompt(prompt);

    // Block here waiting for user input
    bool ok = (fgets(buf, buf_size, stdin) != nullptr);

    // Signal monitor it can resume
    g_input_active.store(false);

    // If fgets failed (stdin closed by signal) → shutdown
    if (!ok) return false;

    // Strip newline
    buf[strcspn(buf, "\n")] = '\0';

    // If g_running went false while we were blocked, treat as quit
    // This suppresses the spurious error message on Ctrl+C
    if (!g_running.load() && strlen(buf) == 0) return false;

    // Strip leading/trailing spaces
    int start = 0;
    while (buf[start] == ' ') start++;
    if (start > 0) memmove(buf, buf + start,
                           strlen(buf) - start + 1);
    int end = (int)strlen(buf) - 1;
    while (end >= 0 && buf[end] == ' ') buf[end--] = '\0';

    return true;
}

static bool is_quit(const char* s) {
    return strcmp(s,"quit")==0 || strcmp(s,"exit")==0 || strcmp(s,"q")==0;
}

// ============================================================
//  producer_thread() — Automatic mode
// ============================================================
void* producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;
    ProducerStyle style     = a->style;

    const char* sn = "?";
    int delay = 0;
    switch (style) {
        case ProducerStyle::SLOW:  sn="SLOW";  delay=800000; break;
        case ProducerStyle::FAST:  sn="FAST";  delay=200000; break;
        case ProducerStyle::BURST: sn="BURST"; delay=50000;  break;
    }

    logger_log(ThreadType::PRODUCER, id,
               std::string("Started [") + sn + " mode].");

    while (g_running.load()) {
        if (style == ProducerStyle::BURST) {
            for (int b = 0; b < 4 && g_running.load(); b++) {
                Transaction txn = make_transaction(id);
                usleep(TRANSITION_DELAY_US);
                shm_buffer_produce(buf, txn);
                logger_log(ThreadType::PRODUCER, id,
                    "TXN #" + std::to_string(txn.transaction_id)
                    + " ──► SHM [" + std::to_string(shm_buffer_count(buf))
                    + "/" + std::to_string(SHARED_BUFFER_SIZE) + "]");
                usleep(delay);
            }
            usleep(2000000);
        } else {
            Transaction txn = make_transaction(id);
            usleep(TRANSITION_DELAY_US);
            shm_buffer_produce(buf, txn);
            logger_log(ThreadType::PRODUCER, id,
                "TXN #" + std::to_string(txn.transaction_id)
                + " ──► SHM [" + std::to_string(shm_buffer_count(buf))
                + "/" + std::to_string(SHARED_BUFFER_SIZE) + "]");
            usleep(delay);
        }
    }

    logger_log(ThreadType::PRODUCER, id, "Stopped.");
    return nullptr;
}

// ============================================================
//  manual_producer_thread() — Step-by-step wizard
//
//  FLOW PER TRANSACTION:
//    Step 1 → show user list   → read 1-5
//    Step 2 → show type menu   → read 1-3
//    Step 3 → show amount      → read number
//    Step 4 → confirm          → read Enter or 'c'
//    → submit → queued panel
//    → ask another?
//
//  The wizard_read_line() helper pauses the monitor at every
//  prompt, so snapshots never interrupt the input flow.
//  Each step completes fully before moving to the next.
// ============================================================
void* manual_producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;

    logger_log(ThreadType::PRODUCER, id,
               "Manual wizard started.");

    char line[128];

    while (g_running.load()) {

        // ════════════════════════════════════════
        //  STEP 1 — SELECT USER
        // ════════════════════════════════════════
        ui_wizard_show_users();

        if (!wizard_read_line("Select user [1-5] or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id, "User quit.");
            break;
        }

        int user_choice = atoi(line);
        if (user_choice < 1 || user_choice > 5) {
            ui_wizard_error("Please enter a number between 1 and 5.");
            continue;   // restart from Step 1
        }

        int         user_id   = USERS[user_choice-1].id;
        const char* user_name = USERS[user_choice-1].name;
        bool        has_sess  = USERS[user_choice-1].has_session;

        // Warn if no session — but stay on same transaction
        if (!has_sess) {
            ui_wizard_warn_no_session(user_name);
            if (!wizard_read_line("Continue anyway? [y/n]: ",
                                   line, sizeof(line))) break;
            if (line[0] != 'y' && line[0] != 'Y') {
                ui_wizard_show_cancelled();
                continue;   // restart from Step 1
            }
        }

        // ════════════════════════════════════════
        //  STEP 2 — SELECT TYPE
        // ════════════════════════════════════════
        ui_wizard_show_types(user_id, user_name);

        if (!wizard_read_line("Select type [1-3] or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id, "User quit.");
            break;
        }

        int type_choice = atoi(line);
        if (type_choice < 1 || type_choice > 3) {
            ui_wizard_error("Please enter 1, 2, or 3.");
            continue;   // restart from Step 1
        }

        const char* txn_type = TXN_TYPES[type_choice - 1];

        // ════════════════════════════════════════
        //  STEP 3 — ENTER AMOUNT
        // ════════════════════════════════════════
        double balance = db_get_balance(user_id);
        if (balance < 0) balance = 0.0;

        ui_wizard_show_amount(user_id, user_name, txn_type, balance);

        if (!wizard_read_line("Enter amount or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id, "User quit.");
            break;
        }

        double amount = atof(line);

        if (amount <= 0) {
            ui_wizard_error("Amount must be greater than zero.");
            continue;
        }
        if (amount > 10000) {
            ui_wizard_error("Maximum is $10,000 per transaction.");
            continue;
        }

        // Overdraft warning
        if ((strcmp(txn_type,"WITHDRAWAL")==0 ||
             strcmp(txn_type,"TRANSFER")  ==0) &&
             balance > 0 && amount > balance) {
            ui_wizard_error(
                "Amount exceeds balance — Validator WILL REJECT this.");
            if (!wizard_read_line("Submit anyway? [y/n]: ",
                                   line, sizeof(line))) break;
            if (line[0] != 'y' && line[0] != 'Y') {
                ui_wizard_show_cancelled();
                continue;
            }
        }

        // ════════════════════════════════════════
        //  STEP 4 — CONFIRM
        // ════════════════════════════════════════
        ui_wizard_show_confirm(user_id, user_name, txn_type, amount);

        if (!wizard_read_line(
                "Press Enter to CONFIRM or 'c' to cancel: ",
                line, sizeof(line))) break;

        if (line[0] == 'c' || line[0] == 'C') {
            ui_wizard_show_cancelled();
            continue;
        }

        // ════════════════════════════════════════
        //  SUBMIT TRANSACTION
        // ════════════════════════════════════════
        Transaction txn;
        txn.transaction_id = g_next_txn_id.fetch_add(1);
        txn.user_id        = user_id;
        txn.amount         = amount;
        strncpy(txn.transaction_type, txn_type, MAX_TYPE_LEN - 1);
        txn.timestamp      = time(nullptr);
        txn.retry_count    = 0;

        db_insert_raw_transaction(txn.transaction_id,
                                  txn.user_id, txn.amount,
                                  std::string(txn.transaction_type));

        usleep(TRANSITION_DELAY_US);
        shm_buffer_produce(buf, txn);

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

        // ════════════════════════════════════════
        //  ASK: ANOTHER?
        // ════════════════════════════════════════
        ui_wizard_ask_another();

        if (!wizard_read_line(" Choice: ",
                               line, sizeof(line))) break;

        if (is_quit(line) ||
            line[0] == 'q' || line[0] == 'Q' ||
            line[0] == '0') {
            logger_log(ThreadType::PRODUCER, id, "User quit.");
            break;
        }
        // '1' or Enter → loop back to Step 1
    }

    g_input_active.store(false);   // make sure flag is cleared on exit
    logger_log(ThreadType::PRODUCER, id,
               "Manual producer thread exiting.");
    return nullptr;
}