#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "shared_buffer.h"

// Arguments passed to validator threads, defining shared buffers and communication pipes.
struct ValidatorArgs {
    SharedMemoryBuffer* buffer;
    int                 write_fd;
    int                 thread_id;
};

// Entry point for validator threads; synchronizes with producers via shared memory semaphores.
void* validator_thread(void* args);

void reject_transaction(Transaction &txn, const char *reason,
                               int thread_id);

#endif // VALIDATOR_H