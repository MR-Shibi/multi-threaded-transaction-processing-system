// ============================================================
//  producer.cpp
//  FIXES:
//    1. Transfer asks for recipient (Step 2b)
//    2. "Choice:" prompt never duplicates
//    3. Arrow keys / escape sequences stripped from input
//    4. g_input_active set for EVERY fgets() including "Another?"
//    5. Better log messages (human-readable)
//    6. Auto mode uses AUTO_TRANSITION_DELAY_US from ui.h
// ============================================================

#include "producer.h"
#include "transaction.h"
#include "shared_buffer.h"
#include "database.h"
#include "logger.h"
#include "ui.h"
#include "monitor.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <string>
#include <cctype>

extern std::atomic<bool> g_running;
extern std::atomic<int>  g_next_txn_id;

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
    {5, "Eve",     false},
};
static const int NUM_USERS = 5;

static const char* TXN_TYPES[] = {"DEPOSIT", "WITHDRAWAL", "TRANSFER"};

// ============================================================
//  HELPER: get_user_name()
//  Returns name string for a given user id, or "Unknown"
// ============================================================
static const char* get_user_name(int uid) {
    for (int i = 0; i < NUM_USERS; i++)
        if (USERS[i].id == uid) return USERS[i].name;
    return "Unknown";
}

// ============================================================
//  HELPER: strip_input()
//  Strips:
//    - Trailing newline / carriage return
//    - Leading and trailing spaces
//    - ANSI/VT100 escape sequences (arrow keys produce ^[[A etc.)
//      These arrive as 0x1B 0x5B 0x41/0x42/0x43/0x44
//  Returns cleaned string in-place.
// ============================================================
static void strip_input(char* buf) {
    // Remove ANSI escape sequences first
    // Pattern: ESC [ ... letter  (ESC = 0x1B)
    char clean[256];
    int j = 0;
    for (int i = 0; buf[i] != '\0'; i++) {
        if ((unsigned char)buf[i] == 0x1B) {
            // Skip ESC and everything until a letter A-Z a-z
            i++;  // skip '['
            while (buf[i] != '\0' &&
                   !((buf[i] >= 'A' && buf[i] <= 'Z') ||
                     (buf[i] >= 'a' && buf[i] <= 'z'))) {
                i++;
            }
            // skip the final letter too (loop will i++ again)
            continue;
        }
        clean[j++] = buf[i];
    }
    clean[j] = '\0';
    memcpy(buf, clean, j + 1);

    // Strip trailing newline / carriage return / spaces
    int end = (int)strlen(buf) - 1;
    while (end >= 0 &&
           (buf[end] == '\n' || buf[end] == '\r' || buf[end] == ' '))
        buf[end--] = '\0';

    // Strip leading spaces
    int start = 0;
    while (buf[start] == ' ') start++;
    if (start > 0)
        memmove(buf, buf + start, strlen(buf) - start + 1);
}

// ============================================================
//  HELPER: wizard_read_line()
//  Sets g_input_active=true BEFORE the prompt so the monitor
//  stays silent while the user is typing.
//  Sets g_input_active=false AFTER fgets() returns.
//  Strips escape sequences from the result.
// ============================================================
static bool wizard_read_line(const char* prompt,
                              char* buf, int buf_size) {
    g_input_active.store(true);
    ui_wizard_prompt(prompt);

    bool ok = (fgets(buf, buf_size, stdin) != nullptr);

    g_input_active.store(false);

    if (!ok) return false;

    strip_input(buf);

    // If shutdown happened while we were waiting → treat as quit
    if (!g_running.load() && strlen(buf) == 0) return false;

    return true;
}

static bool is_quit(const char* s) {
    return strcmp(s,"quit")==0 || strcmp(s,"exit")==0 || strcmp(s,"q")==0;
}

// ============================================================
//  AUTOMATIC TRANSACTION HELPER
// ============================================================
static Transaction make_transaction(int thread_id) {
    static const char* AUTO_TYPES[] = {"DEPOSIT","WITHDRAWAL","TRANSFER"};
    Transaction txn;
    txn.transaction_id = g_next_txn_id.fetch_add(1);
    txn.user_id        = USERS[rand() % NUM_USERS].id;
    txn.amount         = (double)((rand() % 19) + 1) * 50.0;

    int type_idx = rand() % 3;
    strncpy(txn.transaction_type, AUTO_TYPES[type_idx], MAX_TYPE_LEN - 1);

    // For auto TRANSFER, pick a random different user as recipient
    // We store recipient_id in a note field (not in original struct —
    // we embed it in the log message only for auto mode)
    txn.timestamp   = time(nullptr);
    txn.retry_count = 0;

    db_insert_raw_transaction(txn.transaction_id, txn.user_id,
                              txn.amount,
                              std::string(txn.transaction_type));

    // Human-readable log message
    std::string msg = "New transaction created  |  "
        + std::string(get_user_name(txn.user_id))
        + "  |  " + txn.transaction_type
        + "  |  $" + std::to_string((int)txn.amount);

    logger_log(ThreadType::PRODUCER, thread_id, msg);
    return txn;
}

// ============================================================
//  producer_thread() — Automatic mode
//  Uses AUTO_TRANSITION_DELAY_US from ui.h (configurable)
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
               std::string("Auto producer started  [") + sn + " mode]");

    while (g_running.load()) {
        if (style == ProducerStyle::BURST) {
            for (int b = 0; b < 4 && g_running.load(); b++) {
                Transaction txn = make_transaction(id);

                // Configurable delay — change AUTO_TRANSITION_DELAY_US in ui.h
                usleep(AUTO_TRANSITION_DELAY_US);

                shm_buffer_produce(buf, txn);

                logger_log(ThreadType::PRODUCER, id,
                    "Transaction #" + std::to_string(txn.transaction_id)
                    + " placed in queue  ["
                    + std::to_string(shm_buffer_count(buf)) + "/"
                    + std::to_string(SHARED_BUFFER_SIZE) + " slots used]");

                usleep(delay);
            }
            usleep(2000000);  // 2-second pause between bursts

        } else {
            Transaction txn = make_transaction(id);

            usleep(AUTO_TRANSITION_DELAY_US);

            shm_buffer_produce(buf, txn);

            logger_log(ThreadType::PRODUCER, id,
                "Transaction #" + std::to_string(txn.transaction_id)
                + " placed in queue  ["
                + std::to_string(shm_buffer_count(buf)) + "/"
                + std::to_string(SHARED_BUFFER_SIZE) + " slots used]");

            usleep(delay);
        }
    }

    logger_log(ThreadType::PRODUCER, id, "Auto producer stopped.");
    return nullptr;
}

// ============================================================
//  manual_producer_thread() — Step-by-step wizard
//
//  FLOW:
//    Step 1  → Choose account holder (1-5)
//    Step 2  → Choose transaction type (1=DEPOSIT 2=WITHDRAWAL 3=TRANSFER)
//    Step 2b → If TRANSFER: choose recipient (shown only for transfers)
//    Step 3  → Enter amount
//    Step 4  → Review and confirm
//    Result  → ACCEPTED panel + progress logs
//    Then    → Ask to make another (single prompt, no duplicate)
// ============================================================
void* manual_producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;

    logger_log(ThreadType::PRODUCER, id,
               "Manual input wizard is ready.");

    char line[128];

    while (g_running.load()) {

        // ════════════════════════════════════════
        //  STEP 1 — SELECT ACCOUNT HOLDER
        // ════════════════════════════════════════
        ui_wizard_show_users();

        if (!wizard_read_line("Your choice [1-5] or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id, "User exited wizard.");
            break;
        }

        int user_choice = atoi(line);
        if (user_choice < 1 || user_choice > 5) {
            ui_wizard_error("Please enter a number between 1 and 5.");
            continue;
        }

        int         user_id   = USERS[user_choice-1].id;
        const char* user_name = USERS[user_choice-1].name;
        bool        has_sess  = USERS[user_choice-1].has_session;

        // Warn if no session
        if (!has_sess) {
            ui_wizard_warn_no_session(user_name);
            if (!wizard_read_line("Continue anyway? [y/n]: ",
                                   line, sizeof(line))) break;
            if (line[0] != 'y' && line[0] != 'Y') {
                ui_wizard_show_cancelled();
                continue;
            }
        }

        // ════════════════════════════════════════
        //  STEP 2 — SELECT TRANSACTION TYPE
        // ════════════════════════════════════════
        ui_wizard_show_types(user_id, user_name);

        if (!wizard_read_line("Your choice [1-3] or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id, "User exited wizard.");
            break;
        }

        int type_choice = atoi(line);
        if (type_choice < 1 || type_choice > 3) {
            ui_wizard_error("Please enter 1, 2, or 3.");
            continue;
        }

        const char* txn_type = TXN_TYPES[type_choice - 1];

        // ════════════════════════════════════════
        //  STEP 2b — SELECT RECIPIENT (TRANSFER only)
        // ════════════════════════════════════════
        int         recipient_id   = 0;
        const char* recipient_name = nullptr;

        if (strcmp(txn_type, "TRANSFER") == 0) {
            ui_wizard_show_transfer_recipient(user_id, user_name);

            if (!wizard_read_line("Recipient number or 'q' to quit: ",
                                   line, sizeof(line))) break;
            if (is_quit(line)) {
                logger_log(ThreadType::PRODUCER, id, "User exited wizard.");
                break;
            }

            int rec_choice = atoi(line);

            // Validate: 1-5, not the sender
            if (rec_choice < 1 || rec_choice > 5) {
                ui_wizard_error("Please enter a number between 1 and 5.");
                continue;
            }
            if (rec_choice == user_choice) {
                ui_wizard_error("You cannot transfer money to yourself.");
                continue;
            }

            recipient_id   = USERS[rec_choice-1].id;
            recipient_name = USERS[rec_choice-1].name;
        }

        // ════════════════════════════════════════
        //  STEP 3 — ENTER AMOUNT
        // ════════════════════════════════════════
        double balance = db_get_balance(user_id);
        if (balance < 0) balance = 0.0;

        ui_wizard_show_amount(user_id, user_name, txn_type, balance,
                              recipient_id, recipient_name);

        if (!wizard_read_line("Enter amount or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) {
            logger_log(ThreadType::PRODUCER, id, "User exited wizard.");
            break;
        }

        double amount = atof(line);

        if (amount <= 0) {
            ui_wizard_error("Amount must be greater than zero.");
            continue;
        }
        if (amount > 10000) {
            ui_wizard_error("Maximum per transaction is $10,000.");
            continue;
        }

        // Overdraft warning
        if ((strcmp(txn_type,"WITHDRAWAL")==0 ||
             strcmp(txn_type,"TRANSFER")  ==0) &&
             balance > 0 && amount > balance) {
            ui_wizard_error(
                "Amount exceeds your balance. "
                "The validator will reject this.");
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
        ui_wizard_show_confirm(user_id, user_name, txn_type, amount,
                               recipient_id, recipient_name);

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

        // Transition delay
        usleep(AUTO_TRANSITION_DELAY_US);
        shm_buffer_produce(buf, txn);

        // Show success panel
        ui_wizard_show_queued(txn.transaction_id,
                              user_id, user_name,
                              txn_type, amount,
                              recipient_id, recipient_name);

        // Human-readable log message
        std::string log_msg =
            "Transaction #" + std::to_string(txn.transaction_id)
            + " submitted  |  " + std::string(user_name)
            + "  |  " + txn_type
            + "  |  $" + std::to_string((int)amount);

        if (recipient_id > 0)
            log_msg += "  |  To: " + std::string(recipient_name);

        log_msg += "  |  Now in validation queue";
        logger_log(ThreadType::PRODUCER, id, log_msg);

        // ════════════════════════════════════════
        //  ASK: ANOTHER TRANSACTION?
        //  FIX: wizard_read_line() prints the prompt ONCE
        //       g_input_active is set inside wizard_read_line
        //       so monitor won't fire and cause "Choice: Choice:"
        // ════════════════════════════════════════
        ui_wizard_ask_another();

        if (!wizard_read_line("Your choice: ",
                               line, sizeof(line))) break;

        if (is_quit(line) ||
            line[0] == 'q' || line[0] == 'Q' ||
            line[0] == '0') {
            logger_log(ThreadType::PRODUCER, id, "User exited wizard.");
            break;
        }
        // '1' or Enter → loop back to Step 1
    }

    g_input_active.store(false);
    logger_log(ThreadType::PRODUCER, id,
               "Manual input wizard closed.");
    return nullptr;
}