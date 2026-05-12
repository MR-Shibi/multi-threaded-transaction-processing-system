#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include <semaphore.h>
#include <pthread.h>
#include "Transaction.h"

static const int SHARED_BUFFER_SIZE = 8;

// Shared memory structure containing a circular buffer and OS synchronization primitives (mutex and semaphores).
struct SharedMemoryBuffer {
    Transaction data[SHARED_BUFFER_SIZE];
    int  head;
    int  tail;
    int  count;
    sem_t sem_empty;
    sem_t sem_filled;
    pthread_mutex_t mutex;
};

static constexpr const char* SHM_NAME = "/txn_shared_buffer";

// Allocates and initializes a shared memory segment for inter-thread transaction transfer.
SharedMemoryBuffer* shm_buffer_create();
// Maps an existing shared memory segment into the process address space.
SharedMemoryBuffer* shm_buffer_attach();
// Releases the shared memory segment and cleans up associated OS synchronization objects.
void shm_buffer_destroy(SharedMemoryBuffer* buf);
// Thread-safe producer function; uses semaphores to wait for space and a mutex for atomic insertion.
void shm_buffer_produce(SharedMemoryBuffer* buf, const Transaction& txn);
// Thread-safe consumer function; uses semaphores to wait for data and a mutex for atomic removal.
Transaction shm_buffer_consume(SharedMemoryBuffer* buf);
// Returns the number of items currently in the buffer, synchronized via a mutex.
int shm_buffer_count(SharedMemoryBuffer* buf);

#endif // SHARED_BUFFER_H