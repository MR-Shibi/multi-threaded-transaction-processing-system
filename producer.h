#ifndef PRODUCER_H
#define PRODUCER_H

// ============================================================
//  producer.h
//  PRODUCER THREADS — Entry point of the pipeline
//
//  Two modes run simultaneously:
//    Automatic: 3 threads generating transactions at different speeds
//    Manual:    1 thread reading from keyboard input
//
//  Both modes write to:
//    1. POSIX Shared Memory Buffer (shm_buffer_produce)
//    2. raw_transactions table in SQLite (db_insert_raw_transaction)
// ============================================================
// ============================================================
//  producer.h
//  PRODUCER THREADS — Entry point of the pipeline
//
//  Two modes run simultaneously:
//    Automatic: 3 threads generating transactions at different speeds
//    Manual:    1 thread reading from keyboard input
//
//  Both modes write to:
//    1. POSIX Shared Memory Buffer (shm_buffer_produce)
//    2. raw_transactions table in SQLite (db_insert_raw_transaction)
// ============================================================

#include "shared_buffer.h"

// ── Producer personalities (for automatic mode) ──────────────
// Each automatic producer thread gets one of these styles.
enum class ProducerStyle {
    SLOW,   // one transaction every ~800ms — simulates light load
    FAST,   // one transaction every ~200ms — simulates heavy load
    BURST   // 4 rapid transactions, then 2 second pause — batch mode
};

// ── Arguments passed to each producer thread ────────────────
// pthread_create() takes one void* — we pack everything here.
struct ProducerArgs {
    SharedMemoryBuffer* buffer;     // Pointer to shared memory buffer
    int                 thread_id;  // 1, 2, or 3 for automatic; 4 for manual
    ProducerStyle       style;      // Only used by automatic producers
    bool                manual;     // true = keyboard input mode
};

// ── Thread functions ─────────────────────────────────────────

// Automatic producer: generates transactions on its own.
// Runs until the global g_running flag is set to false.
void* producer_thread(void* args);

// Manual producer: reads user input from stdin.
// Runs until the user types "quit" or g_running becomes false.
void* manual_producer_thread(void* args);

#endif // PRODUCER_H