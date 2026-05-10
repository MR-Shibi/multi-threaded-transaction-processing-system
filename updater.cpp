// ============================================================
//  updater.cpp — Human-readable log messages
// ============================================================

#include "updater.h"
#include "fifo_queue.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <cstring>
#include <atomic>

extern std::atomic<bool> g_running;
#include <cstdio>
#include <thread>
#include <unistd.h>
#include <string>

static void wrap_in_transaction(const char* query, char* out, int sz) {
    snprintf(out, sz, "BEGIN; %s COMMIT;", query);
}

void* updater_thread(void* args) {
    UpdaterArgs* a = static_cast<UpdaterArgs*>(args);
    int  read_fd   = a->read_fd;
    int  id        = a->thread_id;
    int  committed = 0;
    int  failed    = 0;

    // ── Open per-thread DB connection (Tier 2) ───────────────
    // The updater runs SQL it received from the FIFO.
    // With a private connection + WAL mode, writes from multiple
    // updater threads can proceed concurrently without any mutex.
    sqlite3* conn = db_open_connection();
    if (!conn) {
        logger_log(ThreadType::UPDATER, id,
            "FATAL: could not open DB connection. Thread exiting.");
        return nullptr;
    }

    logger_log(ThreadType::UPDATER, id,
               "Database writer ready. Waiting for validated transactions.");
    ui_set_thread_status("UPDATER", id, "Idle — waiting for queries");

    char raw_query[FIFO_MSG_SIZE];
    char wrapped[FIFO_MSG_SIZE + 32];

    while (fifo_read_query(read_fd, raw_query)) {

        ui_set_thread_status("UPDATER", id, "Executing SQL...");
        if (g_running.load()) usleep(100000); // 100ms transition delay

        wrap_in_transaction(raw_query, wrapped, sizeof(wrapped));
        bool ok = db_execute(conn, std::string(wrapped));  // Tier 2: no mutex

        if (ok) {
            committed++;

            // Parse transaction details from the query for logging.
            // The new SQL format (TOCTOU fix) uses SELECT not VALUES:
            //   "... INSERT INTO transactions(...) SELECT txn_id,user_id,amount,'type'..."
            // We scan from the "SELECT " token inside the INSERT clause.
            int    txn_id       = -1;
            int    user_id      = -1;
            double amount       = 0.0;
            char   type[16]     = "";

            // Find the INSERT's SELECT clause (skip the UPDATE's SELECT if any)
            const char* insert_pos = strstr(raw_query, "INSERT INTO");
            const char* select_pos = insert_pos
                                   ? strstr(insert_pos, "SELECT ")
                                   : nullptr;
            if (select_pos) {
                sscanf(select_pos,
                    "SELECT %d,%d,%lf,'%15[^']'",
                    &txn_id, &user_id, &amount, type);
            }

            std::string msg;
            if (txn_id > 0) {
                db_update_raw_status(txn_id, "DONE");
                msg = "Transaction #" + std::to_string(txn_id)
                    + " SAVED to database  |  "
                    + std::string(type)
                    + " of $" + std::to_string((int)amount)
                    + "  |  Total saved today: "
                    + std::to_string(committed);
                // Simulate "Disk I/O" or DB overhead
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
            ui_history_push(-1, "UNKNOWN", 0, false);
            char st[72]; snprintf(st, 72, "FAILED  total_fail:%d", failed);
            ui_set_thread_status("UPDATER", id, st);
            logger_log(ThreadType::UPDATER, id,
                "Failed to save transaction.  Total failures: "
                + std::to_string(failed));
        }
    }

    fifo_close(read_fd);

    // ── Close per-thread connection ───────────────────────────
    db_close_connection(conn);

    logger_log(ThreadType::UPDATER, id,
        "Database writer finished  |  "
        + std::to_string(committed) + " transactions saved  |  "
        + std::to_string(failed)    + " failed");

    return nullptr;
}