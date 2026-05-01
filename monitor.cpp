// ============================================================
//  monitor.cpp
//  MONITOR THREAD — Full Implementation
//
//  Periodically samples system state and reports to the logger.
//  Never writes to any shared resource — pure read-only observer.
// ============================================================

// ============================================================
//  monitor.cpp — Updated with rich UI snapshot panels
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
        "Started. Reporting every "
        + std::to_string(MONITOR_INTERVAL_SEC) + "s.");

    int    prev_committed = 0;
    time_t prev_time      = time(nullptr);
    int    snapshot_num   = 0;

    do {
        sleep(MONITOR_INTERVAL_SEC);

        snapshot_num++;

        // ── Read all counts ───────────────────────────────────
        int done       = db_count_raw_by_status("DONE");
        int rejected   = db_count_raw_by_status("REJECTED");
        int pending    = db_count_raw_by_status("PENDING");
        int processing = db_count_raw_by_status("PROCESSING");
        int committed  = db_count_committed();
        int buf_count  = shm_buffer_count(buf);

        // ── Compute throughput ────────────────────────────────
        time_t now     = time(nullptr);
        double elapsed = difftime(now, prev_time);
        double tps     = (elapsed > 0)
                         ? (committed - prev_committed) / elapsed
                         : 0.0;
        prev_committed = committed;
        prev_time      = now;

        // ── Draw rich bordered snapshot panel ─────────────────
        // This prints directly — monitor "owns" its panel output.
        // All other output still goes through logger_log().
        ui_print_monitor_snapshot(
            snapshot_num,
            buf_count, SHARED_BUFFER_SIZE,
            done, rejected, pending, processing,
            committed, tps
        );

        // Also send a brief summary to the logger queue
        // so it appears in the correct chronological order
        // among other thread messages.
        logger_log(ThreadType::MONITOR, id,
            "Snapshot #" + std::to_string(snapshot_num)
            + " | DONE:" + std::to_string(done)
            + " REJECTED:" + std::to_string(rejected)
            + " COMMITTED:" + std::to_string(committed)
            + " TPS:" + std::to_string((int)tps));

    } while (g_running.load());

    // ── Final snapshot at shutdown ────────────────────────────
    int final_done      = db_count_raw_by_status("DONE");
    int final_rejected  = db_count_raw_by_status("REJECTED");
    int final_pending   = db_count_raw_by_status("PENDING");
    int final_proc      = db_count_raw_by_status("PROCESSING");
    int final_committed = db_count_committed();
    int final_buf       = shm_buffer_count(buf);

    ui_print_monitor_snapshot(
        snapshot_num + 1,
        final_buf, SHARED_BUFFER_SIZE,
        final_done, final_rejected, final_pending, final_proc,
        final_committed, 0.0
    );

    logger_log(ThreadType::MONITOR, id,
        "Final snapshot complete. Monitor thread exiting.");

    return nullptr;
}