// ============================================================
//  updater.cpp
//  DB UPDATER THREADS — Full Implementation
//
//  The simplest thread in the system by design.
//  Reads SQL strings from the Named Pipe, executes them.
//  Exits when the pipe signals EOF (all writers closed).
// ============================================================

// ============================================================
//  updater.cpp — Updated with UI formatting and transition delay
// ============================================================

#include "updater.h"
#include "fifo_queue.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <string>

// 1-second transition delay: Named Pipe → SQLite commit
static const int TRANSITION_DELAY_US = 1000000;

// ── Wrap SQL in BEGIN/COMMIT for atomicity ────────────────────
static void wrap_in_transaction(const char* query,
                                 char*       out_buf,
                                 int         out_size) {
    snprintf(out_buf, out_size, "BEGIN; %s COMMIT;", query);
}

// ============================================================
//  updater_thread()
// ============================================================
void* updater_thread(void* args) {
    UpdaterArgs* a  = static_cast<UpdaterArgs*>(args);
    int  read_fd    = a->read_fd;
    int  id         = a->thread_id;
    int  committed  = 0;
    int  failed     = 0;

    logger_log(ThreadType::UPDATER, id,
               "Started. Waiting for queries from Named Pipe...");

    char raw_query[FIFO_MSG_SIZE];
    char wrapped[FIFO_MSG_SIZE + 32];

    while (fifo_read_query(read_fd, raw_query)) {

        // ── Transition delay: Pipe → SQLite ───────────────────
        // Makes the commit stage clearly visible
        usleep(TRANSITION_DELAY_US);

        wrap_in_transaction(raw_query, wrapped, sizeof(wrapped));

        bool ok = db_execute(std::string(wrapped));

        if (ok) {
            committed++;

            // Parse key fields from the query for the log
            int    txn_id       = -1;
            int    user_id      = -1;
            double amount       = 0.0;
            char   type[16]     = "";
            double balance_after = 0.0;

            const char* vp = strstr(raw_query, "VALUES(");
            if (vp) {
                sscanf(vp,
                    "VALUES(%d, %d, %lf, '%15[^']', 'PAID', %lf",
                    &txn_id, &user_id, &amount, type, &balance_after);
            }

            std::string msg;
            if (txn_id > 0) {
                msg = "TXN #" + std::to_string(txn_id)
                    + " COMMITTED ──► SQLite | User:"
                    + std::to_string(user_id)
                    + " $" + std::to_string((int)amount)
                    + " " + std::string(type)
                    + " | Balance: $"
                    + std::to_string((int)balance_after)
                    + " | Total: " + std::to_string(committed);
            } else {
                msg = "Query committed. Total: "
                      + std::to_string(committed);
            }
            logger_log(ThreadType::UPDATER, id, msg);

        } else {
            failed++;
            logger_log(ThreadType::UPDATER, id,
                "FAILED to execute query. Total failed: "
                + std::to_string(failed));
        }
    }

    fifo_close(read_fd);

    logger_log(ThreadType::UPDATER, id,
        "Pipe EOF. Stopped. Committed: "
        + std::to_string(committed)
        + " | Failed: " + std::to_string(failed));

    return nullptr;
}