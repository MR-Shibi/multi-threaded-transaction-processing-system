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
// ============================================================
//  monitor.cpp — Monitor Thread
//
//  Respects g_input_active: when the manual wizard is waiting
//  for a keypress, the monitor skips that snapshot cycle
//  entirely so it never clobbers the input prompt.
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
        + std::to_string(MONITOR_INTERVAL_SEC)
        + "s (pauses during manual input).");

    int    prev_committed = 0;
    time_t prev_time      = time(nullptr);
    int    snapshot_num   = 0;

    do {
        // Sleep for the reporting interval
        sleep(MONITOR_INTERVAL_SEC);

        // ── KEY FIX: skip if user is mid-input ───────────────
        // If the wizard is currently waiting for a keypress,
        // do NOT print anything — it would overwrite the prompt
        // and confuse the user. Just skip this cycle silently.
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

        time_t now     = time(nullptr);
        double elapsed = difftime(now, prev_time);
        double tps     = (elapsed > 0)
                         ? (committed - prev_committed) / elapsed
                         : 0.0;
        prev_committed = committed;
        prev_time      = now;

        // Print snapshot only when user is NOT typing
        ui_print_monitor_snapshot(
            snapshot_num,
            buf_count, SHARED_BUFFER_SIZE,
            done, rejected, pending, processing,
            committed, tps
        );

        logger_log(ThreadType::MONITOR, id,
            "Snapshot #" + std::to_string(snapshot_num)
            + " | DONE:" + std::to_string(done)
            + " REJECTED:" + std::to_string(rejected)
            + " COMMITTED:" + std::to_string(committed)
            + " TPS:" + std::to_string((int)tps));

    } while (g_running.load());

    // Final snapshot at shutdown (only if not mid-input)
    if (!g_input_active.load()) {
        int fd = db_count_raw_by_status("DONE");
        int fr = db_count_raw_by_status("REJECTED");
        int fp = db_count_raw_by_status("PENDING");
        int fc = db_count_raw_by_status("PROCESSING");
        int fm = db_count_committed();
        int fb = shm_buffer_count(buf);

        ui_print_monitor_snapshot(
            snapshot_num + 1,
            fb, SHARED_BUFFER_SIZE,
            fd, fr, fp, fc, fm, 0.0
        );
    }

    logger_log(ThreadType::MONITOR, id,
        "Final snapshot complete. Monitor thread exiting.");

    return nullptr;
}