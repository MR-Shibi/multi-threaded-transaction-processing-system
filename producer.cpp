// ============================================================
//  producer.cpp
//  FIXES:
//    1. "Your choice:" prompt: g_input_active set BEFORE the
//       "Another transaction?" panel draws — prevents any log
//       message from appearing between the panel and prompt
//    2. Auto TRANSFER picks a random different user as recipient
//       and stores them in txn.recipient_id / txn.recipient_name
//    3. Arrow-key escape sequences stripped from input
//    4. Human-readable log messages throughout
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

extern std::atomic<bool> g_running;
extern std::atomic<int>  g_next_txn_id;

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

static constexpr const char* TXN_TYPES[] = {"DEPOSIT","WITHDRAWAL","TRANSFER"};

static constexpr const char* get_user_name(int uid) {
    for (int i = 0; i < NUM_USERS; i++)
        if (USERS[i].id == uid) return USERS[i].name;
    return "Unknown";
}

// ── Strip ANSI escape sequences and whitespace from input ────
static void strip_input(char* buf) {
    char clean[256]; int j = 0;
    for (int i = 0; buf[i]; i++) {
        if ((unsigned char)buf[i] == 0x1B) {
            i++;  // skip '['
            while (buf[i] && !((buf[i]>='A'&&buf[i]<='Z')||
                                (buf[i]>='a'&&buf[i]<='z'))) i++;
            continue;
        }
        clean[j++] = buf[i];
    }
    clean[j] = '\0';
    memcpy(buf, clean, j+1);
    int end = (int)strlen(buf)-1;
    while (end>=0 && (buf[end]=='\n'||buf[end]=='\r'||buf[end]==' '))
        buf[end--] = '\0';
    int start = 0;
    while (buf[start]==' ') start++;
    if (start>0) memmove(buf, buf+start, strlen(buf)-start+1);
}

// ── wizard_read_line() ────────────────────────────────────────
// Sets g_input_active=true BEFORE printing the prompt.
// This must be called as the very first output after any panel.
// The monitor will not print while this flag is true.
static bool wizard_read_line(const char* prompt,
                              char* buf, int buf_size) {
    g_input_active.store(true);
    ui_wizard_prompt(prompt);
    bool ok = (fgets(buf, buf_size, stdin) != nullptr);
    g_input_active.store(false);
    if (!ok) return false;
    strip_input(buf);
    if (!g_running.load() && strlen(buf)==0) return false;
    return true;
}

static bool is_quit(const char* s) {
    return strcmp(s,"quit")==0||strcmp(s,"exit")==0||strcmp(s,"q")==0;
}

// ── wait_for_logger() ─────────────────────────────────────────
// Waits 150ms for the logger to flush any queued messages
// BEFORE we draw a wizard panel. Prevents log lines from
// appearing inside the panel or between panel and prompt.
static void wait_for_logger() {
    usleep(150000);  // 150ms — logger typically flushes in < 50ms
}

// ============================================================
//  AUTOMATIC TRANSACTION HELPER
//  For TRANSFER: picks a random different user as recipient
//  and stores them in txn.recipient_id / txn.recipient_name
// ============================================================
static Transaction make_transaction(int thread_id) {
    Transaction txn;
    txn.transaction_id = g_next_txn_id.fetch_add(1);

    // Pick sender
    int sender_idx = rand() % NUM_USERS;
    txn.user_id    = USERS[sender_idx].id;

    txn.amount     = (double)((rand() % 19) + 1) * 50.0;
    int type_idx   = rand() % 3;
    strncpy(txn.transaction_type, TXN_TYPES[type_idx], MAX_TYPE_LEN-1);
    txn.timestamp  = time(nullptr);
    txn.retry_count = 0;

    // For TRANSFER: assign a random different recipient
    if (strcmp(txn.transaction_type, "TRANSFER") == 0) {
        int rec_idx;
        do { rec_idx = rand() % NUM_USERS; }
        while (rec_idx == sender_idx);  // must be different from sender

        txn.recipient_id = USERS[rec_idx].id;
        strncpy(txn.recipient_name, USERS[rec_idx].name, MAX_NAME_LEN-1);
    }

    db_insert_raw_transaction(txn.transaction_id, txn.user_id,
                              txn.amount,
                              std::string(txn.transaction_type));

    std::string msg = "New transaction created  |  "
        + std::string(get_user_name(txn.user_id))
        + "  |  " + txn.transaction_type
        + "  |  $" + std::to_string((int)txn.amount);

    // Show recipient for TRANSFER
    if (txn.recipient_id > 0)
        msg += "  |  To: " + std::string(txn.recipient_name);

    logger_log(ThreadType::PRODUCER, thread_id, msg);
    return txn;
}

// ============================================================
//  producer_thread() — Automatic mode
// ============================================================
void* producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;
    ProducerStyle style     = a->style;

    const char* sn = "?"; int delay = 0;
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
                usleep(AUTO_TRANSITION_DELAY_US);
                shm_buffer_produce(buf, txn);
                logger_log(ThreadType::PRODUCER, id,
                    "Transaction #" + std::to_string(txn.transaction_id)
                    + " placed in queue  ["
                    + std::to_string(shm_buffer_count(buf)) + "/"
                    + std::to_string(SHARED_BUFFER_SIZE) + " slots used]");
                usleep(delay);
            }
            usleep(2000000);
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
//  KEY FIX for "Your choice:" issue:
//    Before drawing the "Another transaction?" panel we call:
//      g_input_active.store(true)   ← BEFORE the panel draws
//      wait_for_logger()            ← flush any pending log lines
//      draw the panel               ← no log lines can interrupt
//      wizard_read_line()           ← reads input (flag already set)
//    This guarantees the panel and prompt appear clean with
//    no log lines injected between them.
// ============================================================
void* manual_producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;

    logger_log(ThreadType::PRODUCER, id, "Manual input wizard is ready.");

    char line[128];

    while (g_running.load()) {

        // ── STEP 1: Select user ───────────────────────────────
        // Set flag BEFORE drawing panel so no log lines interrupt
        g_input_active.store(true);
        wait_for_logger();
        ui_wizard_show_users();
        // wizard_read_line keeps flag true until input received
        if (!wizard_read_line("Your choice [1-5] or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) { logger_log(ThreadType::PRODUCER,id,"User exited wizard."); break; }

        int user_choice = atoi(line);
        if (user_choice < 1 || user_choice > 5) {
            ui_wizard_error("Please enter a number between 1 and 5.");
            continue;
        }

        int         user_id   = USERS[user_choice-1].id;
        const char* user_name = USERS[user_choice-1].name;
        bool        has_sess  = USERS[user_choice-1].has_session;

        if (!has_sess) {
            g_input_active.store(true);
            wait_for_logger();
            ui_wizard_warn_no_session(user_name);
            if (!wizard_read_line("Continue anyway? [y/n]: ",
                                   line, sizeof(line))) break;
            if (line[0]!='y' && line[0]!='Y') {
                ui_wizard_show_cancelled(); continue;
            }
        }

        // ── STEP 2: Select type ───────────────────────────────
        g_input_active.store(true);
        wait_for_logger();
        ui_wizard_show_types(user_id, user_name);
        if (!wizard_read_line("Your choice [1-3] or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) { logger_log(ThreadType::PRODUCER,id,"User exited wizard."); break; }

        int type_choice = atoi(line);
        if (type_choice < 1 || type_choice > 3) {
            ui_wizard_error("Please enter 1, 2, or 3.");
            continue;
        }
        const char* txn_type = TXN_TYPES[type_choice-1];

        // ── STEP 2b: Select recipient (TRANSFER only) ─────────
        int         recipient_id   = 0;
        const char* recipient_name = nullptr;

        if (strcmp(txn_type, "TRANSFER") == 0) {
            g_input_active.store(true);
            wait_for_logger();
            ui_wizard_show_transfer_recipient(user_id, user_name);
            if (!wizard_read_line("Recipient number or 'q' to quit: ",
                                   line, sizeof(line))) break;
            if (is_quit(line)) { logger_log(ThreadType::PRODUCER,id,"User exited wizard."); break; }

            int rec = atoi(line);
            if (rec < 1 || rec > 5) {
                ui_wizard_error("Please enter a number between 1 and 5.");
                continue;
            }
            if (rec == user_choice) {
                ui_wizard_error("You cannot transfer money to yourself.");
                continue;
            }
            recipient_id   = USERS[rec-1].id;
            recipient_name = USERS[rec-1].name;
        }

        // ── STEP 3: Enter amount ──────────────────────────────
        double balance = db_get_balance(user_id);
        if (balance < 0) balance = 0.0;

        g_input_active.store(true);
        wait_for_logger();
        ui_wizard_show_amount(user_id, user_name, txn_type, balance,
                              recipient_id, recipient_name);
        if (!wizard_read_line("Enter amount or 'q' to quit: ",
                               line, sizeof(line))) break;
        if (is_quit(line)) { logger_log(ThreadType::PRODUCER,id,"User exited wizard."); break; }

        double amount = atof(line);
        if (amount <= 0) {
            ui_wizard_error("Amount must be greater than zero."); continue;
        }
        if (amount > 10000) {
            ui_wizard_error("Maximum per transaction is $10,000."); continue;
        }
        if ((strcmp(txn_type,"WITHDRAWAL")==0 ||
             strcmp(txn_type,"TRANSFER")  ==0) &&
             balance > 0 && amount > balance) {
            ui_wizard_error(
                "Amount exceeds your balance. The validator will reject this.");
            if (!wizard_read_line("Submit anyway? [y/n]: ",
                                   line, sizeof(line))) break;
            if (line[0]!='y' && line[0]!='Y') {
                ui_wizard_show_cancelled(); continue;
            }
        }

        // ── STEP 4: Confirm ───────────────────────────────────
        g_input_active.store(true);
        wait_for_logger();
        ui_wizard_show_confirm(user_id, user_name, txn_type, amount,
                               recipient_id, recipient_name);
        if (!wizard_read_line(
                "Press Enter to CONFIRM or 'c' to cancel: ",
                line, sizeof(line))) break;
        if (line[0]=='c' || line[0]=='C') {
            ui_wizard_show_cancelled(); continue;
        }

        // ── Submit ────────────────────────────────────────────
        Transaction txn;
        txn.transaction_id = g_next_txn_id.fetch_add(1);
        txn.user_id        = user_id;
        txn.amount         = amount;
        strncpy(txn.transaction_type, txn_type, MAX_TYPE_LEN-1);
        txn.timestamp      = time(nullptr);
        txn.retry_count    = 0;

        if (recipient_id > 0) {
            txn.recipient_id = recipient_id;
            strncpy(txn.recipient_name, recipient_name, MAX_NAME_LEN-1);
        }

        db_insert_raw_transaction(txn.transaction_id, txn.user_id,
                                  txn.amount,
                                  std::string(txn.transaction_type));

        usleep(AUTO_TRANSITION_DELAY_US);
        shm_buffer_produce(buf, txn);

        // Show success — set flag first so no logs interrupt
        g_input_active.store(true);
        wait_for_logger();
        ui_wizard_show_queued(txn.transaction_id,
                              user_id, user_name,
                              txn_type, amount,
                              recipient_id, recipient_name);
        g_input_active.store(false);

        // Log after panel is drawn
        std::string log_msg =
            "Transaction #" + std::to_string(txn.transaction_id)
            + " submitted  |  " + std::string(user_name)
            + "  |  " + txn_type
            + "  |  $" + std::to_string((int)amount);
        if (recipient_id > 0)
            log_msg += "  |  To: " + std::string(recipient_name);
        log_msg += "  |  Now in validation queue";
        logger_log(ThreadType::PRODUCER, id, log_msg);

        // ── Another transaction? ──────────────────────────────
        // Set flag BEFORE drawing panel so zero log lines can
        // appear between the panel bottom border and the prompt.
        g_input_active.store(true);
        wait_for_logger();
        ui_wizard_ask_another();
        // wizard_read_line keeps flag true until Enter pressed
        if (!wizard_read_line("Your choice: ",
                               line, sizeof(line))) break;

        if (is_quit(line)||line[0]=='q'||line[0]=='Q'||line[0]=='0') {
            logger_log(ThreadType::PRODUCER, id, "User exited wizard.");
            break;
        }
    }

    g_input_active.store(false);
    logger_log(ThreadType::PRODUCER, id, "Manual input wizard closed.");
    return nullptr;
}