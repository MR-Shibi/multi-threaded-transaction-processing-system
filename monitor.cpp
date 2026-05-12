#include "monitor.h"
#include "shared_buffer.h"
#include "database.h"
#include "logger.h"
#include "ui.h"

#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <pthread.h>
#include <string>

extern volatile sig_atomic_t g_running;
extern bool g_input_active;
extern pthread_mutex_t g_input_mutex;


// Internal structure to hold a snapshot of database statistics for reporting.
struct SystemStats {
    int done, rejected, pending, processing, committed;
    int dep, wth, trn;
};

// Helper to query multiple database counters, providing a unified view of the system state.
static SystemStats get_system_stats() {
    SystemStats s;
    s.done       = db_count_raw_by_status("DONE");
    s.rejected   = db_count_raw_by_status("REJECTED");
    s.pending    = db_count_raw_by_status("PENDING");
    s.processing = db_count_raw_by_status("PROCESSING");
    s.committed  = db_count_committed();
    s.dep        = db_count_raw_by_type("DEPOSIT");
    s.wth        = db_count_raw_by_type("WITHDRAWAL");
    s.trn        = db_count_raw_by_type("TRANSFER");
    return s;
}

// Monitor thread function; runs in a loop to periodically update the UI with system performance data.
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

        pthread_mutex_lock(&g_input_mutex);
        bool input_active = g_input_active;
        pthread_mutex_unlock(&g_input_mutex);
        if (input_active) {
            continue;
        }

        snapshot_num++;
        SystemStats s = get_system_stats();
        int buf_count = shm_buffer_count(buf);

        time_t now     = time(nullptr);
        double elapsed = difftime(now, prev_time);
        double tps     = (elapsed > 0)
                         ? (s.committed - prev_committed) / elapsed
                         : 0.0;

        bool changed = (s.committed != prev_committed) ||
                       (s.rejected != prev_rejected) ||
                       (s.pending > 0) || (s.processing > 0) || (buf_count > 0);

        if (changed) {
            ui_update_monitor(
                snapshot_num,
                buf_count, SHARED_BUFFER_SIZE,
                s.done, s.rejected, s.pending, s.processing,
                s.committed, tps,
                s.dep, s.wth, s.trn
            );

            logger_log(ThreadType::MONITOR, id,
                "Update #" + std::to_string(snapshot_num)
                + "  |  Saved:" + std::to_string(s.committed)
                + "  Rejected:" + std::to_string(s.rejected)
                + "  Speed:" + std::to_string((int)tps) + "/sec");
        }

        prev_committed = s.committed;
        prev_rejected  = s.rejected;
        prev_time      = now;

        if (!g_running) break;
    } while (true);

    pthread_mutex_lock(&g_input_mutex);
    bool input_active_fin = g_input_active;
    pthread_mutex_unlock(&g_input_mutex);
    if (!input_active_fin) {
        SystemStats s = get_system_stats();
        ui_update_monitor(
            snapshot_num + 1,
            shm_buffer_count(buf), SHARED_BUFFER_SIZE,
            s.done, s.rejected, s.pending, s.processing, s.committed, 0.0,
            s.dep, s.wth, s.trn
        );
    }

    logger_log(ThreadType::MONITOR, id,
               "System monitor stopped.");

    return nullptr;
}