#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

// ============================================================
//  shared_buffer.h
//  BUFFER 1: Producer → Validator
//  Implemented using POSIX Shared Memory (shm_open + mmap)
//
//  This is a circular queue of Transaction structs that lives
//  in a shared memory segment. All producer and validator
//  threads access this same region of memory.
//
//  Synchronization mechanism:
//    sem_empty  : starts at SHARED_BUFFER_SIZE (all slots free)
//    sem_filled : starts at 0 (nothing to read yet)
//    mutex      : ensures only 1 thread touches head/tail/count
// ============================================================

#include <semaphore.h>    // sem_t, sem_init, sem_wait, sem_post
#include <pthread.h>      // pthread_mutex_t
#include "transaction.h"

// Maximum number of Transaction slots in the circular buffer.
// Tune this based on how fast producers vs validators run.
// Too small → producers block often. Too large → more memory used.
static const int SHARED_BUFFER_SIZE = 8;

// ── The struct that lives inside shared memory ───────────────
// IMPORTANT: This struct must be "plain old data" (POD).
// No constructors, no virtual functions, no std::string.
// Everything must be a raw value or array — no heap pointers.
// The entire struct is placed at the mmap'd address as raw bytes.
struct SharedMemoryBuffer {
    // The circular array of Transaction objects.
    // Each slot is sizeof(Transaction) = 776 bytes.
    Transaction data[SHARED_BUFFER_SIZE];

    int  head;   // Index of next slot to READ from (consumer advances this)
    int  tail;   // Index of next slot to WRITE to (producer advances this)
    int  count;  // How many items currently occupy the buffer

    // Semaphore counting empty slots.
    // Initialized to SHARED_BUFFER_SIZE. Decremented by producer,
    // incremented by consumer.
    sem_t sem_empty;

    // Semaphore counting filled slots.
    // Initialized to 0. Incremented by producer, decremented by consumer.
    sem_t sem_filled;

    // Mutex protecting the critical section (reading/writing data[],
    // advancing head/tail, updating count).
    pthread_mutex_t mutex;
};

// Name of the shared memory segment in the kernel's namespace.
// You can see it as a file at /dev/shm/txn_shared_buffer on Linux.
static constexpr const char* SHM_NAME = "/txn_shared_buffer";

// ── Public API ───────────────────────────────────────────────

// Creates the shared memory segment, maps it, and initializes
// all semaphores and the mutex. Call this ONCE at startup.
SharedMemoryBuffer* shm_buffer_create();

// Attaches to an already-created shared memory segment.
// Used if you want a second process to connect (not needed in Phase 2).
SharedMemoryBuffer* shm_buffer_attach();

// Unmaps and unlinks the shared memory segment. Call at shutdown.
void shm_buffer_destroy(SharedMemoryBuffer* buf);

// Producer calls this to add a transaction to the buffer.
// BLOCKS if the buffer is full (sem_empty == 0).
void shm_buffer_produce(SharedMemoryBuffer* buf, const Transaction& txn);

// Validator calls this to take a transaction from the buffer.
// BLOCKS if the buffer is empty (sem_filled == 0).
Transaction shm_buffer_consume(SharedMemoryBuffer* buf);

// Returns the current number of items in the buffer (thread-safe).
int shm_buffer_count(SharedMemoryBuffer* buf);

#endif // SHARED_BUFFER_H