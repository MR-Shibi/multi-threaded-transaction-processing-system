// ============================================================
//  updater.cpp — Human-readable log messages
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

static void wrap_in_transaction(const char* query, char* out, int sz) {
    snprintf(out, sz, "BEGIN; %s COMMIT;", query);
}

void* updater_thread(void* args) {
    UpdaterArgs* a = static_cast<UpdaterArgs*>(args);
    int  read_fd   = a->read_fd;
    int  id        = a->thread_id;
    int  committed = 0;
    int  failed    = 0;

    logger_log(ThreadType::UPDATER, id,
               "Database writer ready. Waiting for validated transactions.");

    char raw_query[FIFO_MSG_SIZE];
    char wrapped[FIFO_MSG_SIZE + 32];

    while (fifo_read_query(read_fd, raw_query)) {

        usleep(AUTO_TRANSITION_DELAY_US);

        wrap_in_transaction(raw_query, wrapped, sizeof(wrapped));
        bool ok = db_execute(std::string(wrapped));

        if (ok) {
            committed++;

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
                msg = "Transaction #" + std::to_string(txn_id)
                    + " SAVED to database  |  "
                    + std::string(type)
                    + " of $" + std::to_string((int)amount)
                    + "  |  New balance: $"
                    + std::to_string((int)balance_after)
                    + "  |  Total saved today: "
                    + std::to_string(committed);
            } else {
                msg = "Transaction saved.  Total: "
                      + std::to_string(committed);
            }
            logger_log(ThreadType::UPDATER, id, msg);

        } else {
            failed++;
            logger_log(ThreadType::UPDATER, id,
                "Failed to save transaction.  Total failures: "
                + std::to_string(failed));
        }
    }

    fifo_close(read_fd);

    logger_log(ThreadType::UPDATER, id,
        "Database writer finished  |  "
        + std::to_string(committed) + " transactions saved  |  "
        + std::to_string(failed)    + " failed");

    return nullptr;
}