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
extern std::atomic<bool> g_input_active;

static const int AUTO_TRANSITION_DELAY_US = 100000; // 100ms delay for simulation

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

// ── wizard_read_line() ────────────────────────────────────────
static bool wizard_read_line(const char* prompt, char* buf, int buf_size) {
    g_input_active.store(true);
    ui_wizard_get_string(buf, buf_size, prompt);
    g_input_active.store(false);
    if (strlen(buf) == 0) return false;
    return true;
}

static bool is_quit(const char* s) {
    return strcmp(s,"quit")==0||strcmp(s,"exit")==0||strcmp(s,"q")==0;
}

static Transaction make_transaction(int thread_id) {
    Transaction txn;
    txn.transaction_id = g_next_txn_id.fetch_add(1);

    int sender_idx = rand() % NUM_USERS;
    txn.user_id    = USERS[sender_idx].id;

    txn.amount     = (double)((rand() % 19) + 1) * 50.0;
    int type_idx   = rand() % 3;
    strncpy(txn.transaction_type, TXN_TYPES[type_idx], MAX_TYPE_LEN-1);
    txn.timestamp  = time(nullptr);
    txn.retry_count = 0;

    if (strcmp(txn.transaction_type, "TRANSFER") == 0) {
        int rec_idx;
        do { rec_idx = rand() % NUM_USERS; }
        while (rec_idx == sender_idx);

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

    if (txn.recipient_id > 0)
        msg += "  |  To: " + std::string(txn.recipient_name);

    logger_log(ThreadType::PRODUCER, thread_id, msg);
    return txn;
}

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
    ui_set_thread_status("PRODUCER", id, (std::string(sn) + " \u2014 started").c_str());

    while (g_running.load()) {
        if (style == ProducerStyle::BURST) {
            for (int b = 0; b < 4 && g_running.load(); b++) {
                Transaction txn = make_transaction(id);
                usleep(AUTO_TRANSITION_DELAY_US);
                shm_buffer_produce(buf, txn);
                char st[72]; snprintf(st, 72, "[%s] Queued #%d", sn, txn.transaction_id);
                ui_set_thread_status("PRODUCER", id, st);
                ui_queue_push(txn.transaction_id, get_user_name(txn.user_id), txn.transaction_type, txn.amount);
                logger_log(ThreadType::PRODUCER, id,
                    "Transaction #" + std::to_string(txn.transaction_id)
                    + " placed in queue");
                usleep(delay);
            }
            usleep(2000000);
        } else {
            Transaction txn = make_transaction(id);
            usleep(AUTO_TRANSITION_DELAY_US);
            shm_buffer_produce(buf, txn);
            char st2[72]; snprintf(st2, 72, "[%s] Queued #%d", sn, txn.transaction_id);
            ui_set_thread_status("PRODUCER", id, st2);
            ui_queue_push(txn.transaction_id, get_user_name(txn.user_id), txn.transaction_type, txn.amount);
            logger_log(ThreadType::PRODUCER, id,
                "Transaction #" + std::to_string(txn.transaction_id)
                + " placed in queue");
            usleep(delay);
        }
    }

    logger_log(ThreadType::PRODUCER, id, "Auto producer stopped.");
    return nullptr;
}

void* manual_producer_thread(void* args) {
    ProducerArgs* a         = static_cast<ProducerArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id                  = a->thread_id;
    bool manual_only        = a->manual;

    logger_log(ThreadType::PRODUCER, id, "Manual input wizard is ready.");

    char line[128];

    while (g_running.load()) {
        ui_wizard_clear();
        ui_wizard_print(0, 0, "Select User (1-5):", CP_HEADER);
        for(int i=0; i<5; i++) {
            char user_line[64];
            snprintf(user_line, sizeof(user_line), "[%d] %s", USERS[i].id, USERS[i].name);
            ui_wizard_print(1, i*12, user_line, USERS[i].has_session ? CP_SUCCESS : CP_ERROR);
        }

        if (!wizard_read_line("Your choice (q to quit): ", line, sizeof(line))) break;
        if (is_quit(line)) break;

        int user_choice = atoi(line);
        if (user_choice < 1 || user_choice > 5) continue;

        int user_id = USERS[user_choice-1].id;
        const char* user_name = USERS[user_choice-1].name;

        ui_wizard_clear();
        ui_wizard_print(0, 0, "Select Type:", CP_HEADER);
        ui_wizard_print(1, 0, "[1] DEPOSIT  [2] WITHDRAWAL  [3] TRANSFER", CP_PRODUCER);
        if (!wizard_read_line("Type [1-3]: ", line, sizeof(line))) break;
        if (is_quit(line)) break;

        int type_choice = atoi(line);
        if (type_choice < 1 || type_choice > 3) continue;
        const char* txn_type = TXN_TYPES[type_choice-1];

        int recipient_id = 0;
        const char* recipient_name = nullptr;

        if (type_choice == 3) {
            ui_wizard_clear();
            ui_wizard_print(0, 0, "Select Recipient (1-5):", CP_HEADER);
            if (!wizard_read_line("Recipient ID: ", line, sizeof(line))) break;
            int rec = atoi(line);
            if (rec < 1 || rec > 5 || rec == user_choice) continue;
            recipient_id = USERS[rec-1].id;
            recipient_name = USERS[rec-1].name;
        }

        ui_wizard_clear();
        ui_wizard_print(0, 0, "Enter Amount:", CP_HEADER);
        if (!wizard_read_line("Amount $: ", line, sizeof(line))) break;
        double amount = atof(line);
        if (amount <= 0) continue;

        // Confirmation
        ui_wizard_clear();
        char conf[128];
        snprintf(conf, sizeof(conf), "Submit %s of $%.2f for %s?", txn_type, amount, user_name);
        ui_wizard_print(0, 0, conf, CP_HEADER);
        if (!wizard_read_line("Press Enter to confirm (c to cancel): ", line, sizeof(line))) break;
        if (line[0] == 'c') continue;

        // Submit
        Transaction txn;
        txn.transaction_id = g_next_txn_id.fetch_add(1);
        txn.user_id = user_id;
        txn.amount = amount;
        strncpy(txn.transaction_type, txn_type, MAX_TYPE_LEN-1);
        txn.timestamp = time(nullptr);
        if (recipient_id > 0) {
            txn.recipient_id = recipient_id;
            strncpy(txn.recipient_name, recipient_name, MAX_NAME_LEN-1);
        }

        db_insert_raw_transaction(txn.transaction_id, txn.user_id, txn.amount, std::string(txn.transaction_type));
        shm_buffer_produce(buf, txn);
        
        logger_log(ThreadType::PRODUCER, id, "Transaction #" + std::to_string(txn.transaction_id) + " submitted.");
        
        ui_wizard_clear();
        ui_wizard_print(1, 0, "Transaction Queued!", CP_SUCCESS);
        usleep(1000000);
    }

    if (manual_only) g_running.store(false);
    return nullptr;
}