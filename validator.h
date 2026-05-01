#ifndef VALIDATOR_H
#define VALIDATOR_H

// ============================================================
//  validator.h
//  VALIDATOR THREADS
//
//  Each validator thread:
//    1. Consumes a Transaction from Shared Memory (Buffer 1)
//    2. Validates session and balance via SQLite queries
//    3. Builds the SQL INSERT query into txn.commit_query
//    4. Writes the query string into the Named Pipe (Buffer 2)
//
//  A Validator is both a consumer (of Buffer 1)
//  and a producer (into Buffer 2) simultaneously.
// ============================================================

#include "shared_buffer.h"

// ── Arguments passed to each validator thread ────────────────
struct ValidatorArgs {
    SharedMemoryBuffer* buffer;     // Shared memory buffer to READ from
    int                 write_fd;   // Named pipe file descriptor to WRITE to
    int                 thread_id;  // 1, 2, ... (for logging)
};

// The thread function signature required by pthread_create()
void* validator_thread(void* args);

#endif // VALIDATOR_H