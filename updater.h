#ifndef UPDATER_H
#define UPDATER_H

// ============================================================
//  updater.h
//  DB UPDATER THREADS — Final stage of the pipeline
//
//  Each updater thread:
//    1. Reads a SQL query string from the Named Pipe (FIFO)
//    2. Wraps it in BEGIN/COMMIT for atomicity
//    3. Executes it against SQLite via db_execute()
//    4. Logs the result
//    5. Loops until pipe EOF (all validators have closed)
//
//  The updater has zero business logic — it is a pure executor.
//  All logic lives in the Validator that built the query.
// ============================================================

// ── Arguments passed to each updater thread ─────────────────
struct UpdaterArgs {
    int read_fd;    // Named pipe file descriptor to READ from
    int thread_id;  // 1, 2, ... (for logging)
};

// The thread function signature required by pthread_create()
void* updater_thread(void* args);

#endif // UPDATER_H