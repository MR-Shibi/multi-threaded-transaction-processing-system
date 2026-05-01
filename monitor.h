#ifndef MONITOR_H
#define MONITOR_H

// ============================================================
//  monitor.h
//  MONITOR THREAD — Read-only system observer
//
//  Wakes every MONITOR_INTERVAL_SEC seconds, queries the
//  database and shared memory buffer, and sends a formatted
//  snapshot to the logger thread.
//
//  Rules:
//    - NEVER writes to any buffer
//    - NEVER writes to the database
//    - NEVER touches the Named Pipe
//    - Only reads — a pure observer
// ============================================================

#include "shared_buffer.h"

// How often the monitor wakes up and reports (in seconds)
static const int MONITOR_INTERVAL_SEC = 2;

// ── Arguments passed to the monitor thread ──────────────────
struct MonitorArgs {
    SharedMemoryBuffer* buffer;     // To read current occupancy
    int                 thread_id;  // For log labeling (always 1)
};

// The thread function
void* monitor_thread(void* args);

#endif // MONITOR_H