#include "updater.h"
#include "Transaction.h"
#include "fifo_queue.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <cstring>
#include <pthread.h>
#include <cstdio>
#include <thread>
#include <unistd.h>
#include <string>
#include <csignal>

extern volatile sig_atomic_t g_running;


// Wraps a raw SQL query in a BEGIN/COMMIT block to ensure atomic database transactions.
static void wrap_in_transaction(const char* query, char* out, int sz) {
    snprintf(out, sz, "BEGIN; %s COMMIT;", query);
}

// Entry point for database updater threads; synchronizes with validators via the FIFO named pipe.
void* updater_thread(void* args) {
    UpdaterArgs* a = static_cast<UpdaterArgs*>(args);
    int  read_fd   = a->read_fd;
    int  id        = a->thread_id;
    int  committed = 0;
    int  failed    = 0;

    sqlite3* conn = db_open_connection();
    if (!conn) {
        logger_log(ThreadType::UPDATER, id,
            "FATAL: could not open DB connection. Thread exiting.");
        return nullptr;
    }

    logger_log(ThreadType::UPDATER, id,
               "Database writer ready. Waiting for validated transactions.");
    ui_set_thread_status("UPDATER", id, "Idle - waiting for queries");

    char raw_query[FIFO_MSG_SIZE];
    char wrapped[FIFO_MSG_SIZE + 32];

    while (fifo_read_query(read_fd, raw_query)) {
        if (strcmp(raw_query, SHUTDOWN_QUERY) == 0) {
            logger_log(ThreadType::UPDATER, id, "Received shutdown signal via FIFO. Stopping.");
            break;
        }

        ui_set_thread_status("UPDATER", id, "Executing SQL...");
        if (g_running) usleep(100000);

        int    txn_id   = -1;
        int    user_id  = -1;
        double amount   = 0.0;
        char   type[16] = "";

        const char* insert_pos = strstr(raw_query, "INSERT INTO");
        const char* select_pos =
            insert_pos ? strstr(insert_pos, "SELECT ") : nullptr;
        if (select_pos) {
            sscanf(select_pos, "SELECT %d,%d,%lf,'%15[^']'", &txn_id, &user_id,
                   &amount, type);
        }

        wrap_in_transaction(raw_query, wrapped, sizeof(wrapped));
        bool ok = db_execute(conn, std::string(wrapped));

        if (ok) {
            committed++;

            std::string msg;
            if (txn_id > 0) {
                db_update_raw_status(txn_id, "DONE");
                msg = "Transaction #" + std::to_string(txn_id)
                    + " SAVED to database  |  "
                    + std::string(type)
                    + " of $" + std::to_string((int)amount)
                    + "  |  Total saved today: "
                    + std::to_string(committed);
                std::this_thread::sleep_for(std::chrono::milliseconds(200 + (rand() % 300)));
                ui_history_push(txn_id, type, amount, true);
                char st[72]; snprintf(st, 72, "Saved #%d (%s $%.0f)  total:%d", txn_id, type, amount, committed);
                ui_set_thread_status("UPDATER", id, st);
            } else {
                msg = "Transaction executed (funds may have been taken by "
                      "concurrent txn).  Total: " + std::to_string(committed);
            }
            logger_log(ThreadType::UPDATER, id, msg);

        } else {
            failed++;
            if (txn_id > 0)
                db_update_raw_status(txn_id, STATUS_FAILED);
            ui_history_push(txn_id > 0 ? txn_id : -1, "UNKNOWN", 0, false);
            char st[72]; snprintf(st, 72, "FAILED  total_fail:%d", failed);
            ui_set_thread_status("UPDATER", id, st);
            logger_log(ThreadType::UPDATER, id,
                "Failed to save transaction.  Total failures: "
                + std::to_string(failed));
        }
    }

    db_close_connection(conn);

    logger_log(ThreadType::UPDATER, id,
        "Database writer finished  |  "
        + std::to_string(committed) + " transactions saved  |  "
        + std::to_string(failed)    + " failed");

    return nullptr;
}