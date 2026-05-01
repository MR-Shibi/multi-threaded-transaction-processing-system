#ifndef MONITOR_H
#define MONITOR_H

// ============================================================
//  monitor.h — Monitor Thread
//
//  Wakes every MONITOR_INTERVAL_SEC seconds and prints a
//  live system snapshot.
//
//  KEY ADDITION: g_input_active flag
//    When the manual wizard is waiting for user input,
//    it sets g_input_active = true.
//    The monitor sees this and SKIPS printing that cycle
//    so it never interrupts an active input prompt.
//    Once the wizard finishes a full transaction (or cancels),
//    g_input_active is set back to false.
// ============================================================

#include "shared_buffer.h"
#include <atomic>

// How often the monitor wakes up (seconds)
static const int MONITOR_INTERVAL_SEC = 2;

// ── Global flag: true while wizard is waiting for keypress ──
// Defined in main.cpp, extern'd everywhere it's needed.
extern std::atomic<bool> g_input_active;

// ── Thread arguments ─────────────────────────────────────────
struct MonitorArgs {
    SharedMemoryBuffer* buffer;
    int                 thread_id;
};

void* monitor_thread(void* args);

#endif // MONITOR_H