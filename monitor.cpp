// ============================================================
//  monitor.cpp — Updated with human-readable snapshot labels
//  FIX: WARNING threshold raised to >3 to avoid false alarms
//       when a single transaction is in PENDING briefly
// ============================================================

#include "monitor.h"
#include "shared_buffer.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <atomic>
#include <string>

extern std::atomic<bool> g_running;

void* monitor_thread(void* args) {
    MonitorArgs*        a   = static_cast<MonitorArgs*>(args);
    SharedMemoryBuffer* buf = a->buffer;
    int id = a->thread_id;

    logger_log(ThreadType::MONITOR, id,
        "System monitor started. Updates every "
        + std::to_string(MONITOR_INTERVAL_SEC) + " seconds.");

    int    prev_committed = 0;
    int    prev_rejected  = 0;
    time_t prev_time      = time(nullptr);
    int    snapshot_num   = 0;

    do {
        sleep(MONITOR_INTERVAL_SEC);

        // Skip if user is mid-input — never interrupt the wizard
        if (g_input_active.load()) {
            continue;
        }

        snapshot_num++;

        int done       = db_count_raw_by_status("DONE");
        int rejected   = db_count_raw_by_status("REJECTED");
        int pending    = db_count_raw_by_status("PENDING");
        int processing = db_count_raw_by_status("PROCESSING");
        int committed  = db_count_committed();
        int buf_count  = shm_buffer_count(buf);

        int dep = db_count_raw_by_type("DEPOSIT");
        int wth = db_count_raw_by_type("WITHDRAWAL");
        int trn = db_count_raw_by_type("TRANSFER");

        time_t now     = time(nullptr);
        double elapsed = difftime(now, prev_time);
        double tps     = (elapsed > 0)
                         ? (committed - prev_committed) / elapsed
                         : 0.0;

        bool changed = (committed != prev_committed) ||
                       (rejected != prev_rejected) ||
                       (pending > 0) || (processing > 0) || (buf_count > 0);

        if (changed) {
            ui_print_monitor_snapshot(
                snapshot_num,
                buf_count, SHARED_BUFFER_SIZE,
                done, rejected, pending, processing,
                committed, tps,
                dep, wth, trn
            );

            logger_log(ThreadType::MONITOR, id,
                "Update #" + std::to_string(snapshot_num)
                + "  |  Saved:" + std::to_string(committed)
                + "  Rejected:" + std::to_string(rejected)
                + "  Speed:" + std::to_string((int)tps) + "/sec");
        }

        prev_committed = committed;
        prev_rejected  = rejected;
        prev_time      = now;

    } while (g_running.load());

    // Final update at shutdown
    if (!g_input_active.load()) {
        int fd = db_count_raw_by_status("DONE");
        int fr = db_count_raw_by_status("REJECTED");
        int fp = db_count_raw_by_status("PENDING");
        int fc = db_count_raw_by_status("PROCESSING");
        int fm = db_count_committed();
        int fb = shm_buffer_count(buf);
        int fd1 = db_count_raw_by_type("DEPOSIT");
        int fw1 = db_count_raw_by_type("WITHDRAWAL");
        int ft1 = db_count_raw_by_type("TRANSFER");

        ui_print_monitor_snapshot(
            snapshot_num + 1,
            fb, SHARED_BUFFER_SIZE,
            fd, fr, fp, fc, fm, 0.0,
            fd1, fw1, ft1
        );
    }

    logger_log(ThreadType::MONITOR, id,
               "System monitor stopped.");

    return nullptr;
}