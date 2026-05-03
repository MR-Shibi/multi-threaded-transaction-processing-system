// ============================================================
//  shared_buffer.cpp
//  BUFFER 1: POSIX Shared Memory Circular Queue
//
//  This file implements the actual shared memory operations.
//  Every function here is called by producer or validator threads.
// ============================================================

#include "shared_buffer.h"

#include <sys/mman.h>     // mmap, munmap, PROT_READ, PROT_WRITE, MAP_SHARED
#include <sys/stat.h>     // mode constants like S_IRUSR, S_IWUSR
#include <fcntl.h>        // O_CREAT, O_RDWR for shm_open flags
#include <unistd.h>       // ftruncate, close
#include <cstring>        // memset
#include <stdexcept>      // std::runtime_error
#include <cstdio>
#include "logger.h"

// ============================================================
//  shm_buffer_create()
//
//  Step-by-step:
//  1. shm_open()   → ask the kernel for a named shared memory object
//  2. ftruncate()  → set its size to sizeof(SharedMemoryBuffer)
//  3. mmap()       → map those bytes into our address space
//  4. Initialize   → set up semaphores, mutex, head/tail/count
// ============================================================
SharedMemoryBuffer* shm_buffer_create() {

    // ── Step 1: Create the shared memory object ──────────────
    // shm_open() is like open() for files, but the "file" lives
    // in RAM (at /dev/shm/ on Linux), not on disk.
    //
    // O_CREAT | O_RDWR : create it if it doesn't exist, open for read+write
    // O_TRUNC          : if it already exists (crash leftover), reset it
    // 0666             : read+write permissions for owner and group
    //
    // Returns a file descriptor (fd) — an integer handle to the object.
    // If it fails, returns -1.
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd == -1) {
        throw std::runtime_error("shm_open failed: could not create shared memory");
    }

    // ── Step 2: Set the size of the shared memory object ─────
    // A newly created shared memory object has size 0.
    // ftruncate() sets it to exactly sizeof(SharedMemoryBuffer).
    // This allocates the physical memory pages in the kernel.
    if (ftruncate(fd, sizeof(SharedMemoryBuffer)) == -1) {
        throw std::runtime_error("ftruncate failed: could not resize shared memory");
    }

    // ── Step 3: Map the shared memory into our address space ─
    // mmap() is the magic call. It takes the file descriptor and
    // "projects" those bytes into a range of virtual memory addresses
    // in our process. The return value is a void* pointer to the start.
    //
    // Arguments explained:
    //   NULL              : let the OS choose the virtual address
    //   sizeof(...)       : how many bytes to map
    //   PROT_READ|WRITE   : we want to both read and write
    //   MAP_SHARED        : changes are visible to other processes/threads
    //                       (MAP_PRIVATE would give each thread its own copy)
    //   fd                : the shared memory object to map
    //   0                 : start mapping from the beginning (offset 0)
    void* ptr = mmap(NULL,
                     sizeof(SharedMemoryBuffer),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     fd,
                     0);

    // MAP_FAILED is the error return from mmap (not NULL).
    if (ptr == MAP_FAILED) {
        throw std::runtime_error("mmap failed: could not map shared memory");
    }

    // After mmap(), the file descriptor is no longer needed.
    // The mapping persists independently of the fd.
    close(fd);

    // ── Step 4: Cast the raw void* to our struct type ────────
    // ptr points to raw bytes. We tell the compiler to treat
    // those bytes as a SharedMemoryBuffer. This is the core trick
    // of shared memory — we impose our struct layout on raw RAM.
    SharedMemoryBuffer* buf = static_cast<SharedMemoryBuffer*>(ptr);

    // Zero out the entire struct first to clear any stale data.
    memset(buf, 0, sizeof(SharedMemoryBuffer));

    // ── Step 5: Initialize the semaphores ────────────────────
    // sem_init() initializes an unnamed semaphore in-place
    // (as opposed to sem_open() which creates a named semaphore in /dev/shm).
    //
    // Arguments:
    //   &buf->sem_empty : address of the semaphore
    //   1               : pshared flag — 1 means it's shared between
    //                     THREADS (or processes via shared memory).
    //                     0 would mean only within one process's heap.
    //   SHARED_BUFFER_SIZE : initial value — all slots are empty to start
    if (sem_init(&buf->sem_empty, 1, SHARED_BUFFER_SIZE) != 0) {
        throw std::runtime_error("sem_init failed for sem_empty");
    }

    // sem_filled starts at 0: nothing has been produced yet,
    // so any consumer that calls sem_wait will block immediately.
    if (sem_init(&buf->sem_filled, 1, 0) != 0) {
        throw std::runtime_error("sem_init failed for sem_filled");
    }

    // ── Step 6: Initialize the mutex ─────────────────────────
    // pthread_mutexattr_t lets us configure the mutex.
    // We use PTHREAD_MUTEX_ERRORCHECK which returns an error
    // if a thread tries to lock a mutex it already holds
    // (helps catch bugs during development).
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    // PTHREAD_PROCESS_SHARED: the mutex can be used by threads
    // in different processes (needed because it lives in shared memory).
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    if (pthread_mutex_init(&buf->mutex, &attr) != 0) {
        throw std::runtime_error("pthread_mutex_init failed");
    }
    pthread_mutexattr_destroy(&attr);  // Free the attribute object

    // ── Step 7: Initialize head/tail/count ───────────────────
    // (Already zeroed by memset, but explicit is clearer)
    buf->head  = 0;
    buf->tail  = 0;
    buf->count = 0;

    {
        char _msg[128];
        snprintf(_msg, sizeof(_msg),
                 "Shared memory buffer ready  |  %s  |  %zu bytes",
                 SHM_NAME, sizeof(SharedMemoryBuffer));
        logger_log(ThreadType::SYSTEM, 0, std::string(_msg));
    }

    return buf;
}

// ============================================================
//  shm_buffer_attach()
//  Connect to an already-existing shared memory segment.
//  The segment must have been created by shm_buffer_create().
// ============================================================
SharedMemoryBuffer* shm_buffer_attach() {
    // O_RDWR only — no O_CREAT, because it must already exist
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        throw std::runtime_error("shm_open failed: segment does not exist");
    }

    void* ptr = mmap(NULL,
                     sizeof(SharedMemoryBuffer),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     fd,
                     0);
    if (ptr == MAP_FAILED) {
        throw std::runtime_error("mmap failed in attach");
    }

    close(fd);
    return static_cast<SharedMemoryBuffer*>(ptr);
}

// ============================================================
//  shm_buffer_destroy()
//  Clean up the shared memory segment.
//  Call this once at program shutdown.
// ============================================================
void shm_buffer_destroy(SharedMemoryBuffer* buf) {
    if (buf == nullptr) return;

    // Destroy the semaphores (releases kernel resources)
    sem_destroy(&buf->sem_empty);
    sem_destroy(&buf->sem_filled);

    // Destroy the mutex
    pthread_mutex_destroy(&buf->mutex);

    // Unmap: remove the mapping from this process's address space.
    // After this, accessing buf is undefined behavior.
    munmap(buf, sizeof(SharedMemoryBuffer));

    // Unlink: delete the shared memory object from the kernel.
    // Without this, /dev/shm/txn_shared_buffer persists across reboots
    // (on some systems). It's the equivalent of deleting a file.
    shm_unlink(SHM_NAME);

    logger_log(ThreadType::SYSTEM, 0, "Shared memory segment released and cleaned up.");
}

// ============================================================
//  shm_buffer_produce()
//  Called by a Producer thread to add a transaction.
//
//  THE PROTOCOL (must follow this exact order):
//    1. sem_wait(sem_empty)  — claim one empty slot (blocks if full)
//    2. mutex_lock()         — enter critical section
//    3. write data[tail]     — copy transaction into the slot
//    4. advance tail         — move write position forward (wrap if needed)
//    5. increment count      — update the occupancy counter
//    6. mutex_unlock()       — leave critical section
//    7. sem_post(sem_filled) — signal that one more item is available
//
//  WHY THIS ORDER MATTERS:
//    sem_wait BEFORE lock: if we locked first and then blocked on the
//    semaphore, we'd hold the lock while sleeping — deadlock for every
//    other thread trying to use the buffer.
//
//    sem_post AFTER unlock: if we post before unlocking, a woken
//    consumer immediately tries to lock — and blocks. Post after
//    unlock so the consumer can proceed right away.
// ============================================================
void shm_buffer_produce(SharedMemoryBuffer* buf, const Transaction& txn) {

    // Step 1: Wait for an empty slot.
    // If sem_empty > 0: decrement it and continue immediately.
    // If sem_empty == 0 (buffer full): sleep until a consumer frees a slot.
    sem_wait(&buf->sem_empty);

    // Step 2: Lock the mutex — we're about to touch shared data.
    pthread_mutex_lock(&buf->mutex);

    // ── CRITICAL SECTION START ────────────────────────────────
    // Only one thread executes these lines at a time.

    // Step 3: Copy the transaction into the current tail slot.
    // We use assignment (not memcpy) — the Transaction struct supports it
    // because all fields are plain types or char arrays.
    buf->data[buf->tail] = txn;

    // Step 4: Advance tail, wrapping around using modulo.
    // Example: if SHARED_BUFFER_SIZE=8 and tail=7, next tail = (7+1)%8 = 0
    buf->tail = (buf->tail + 1) % SHARED_BUFFER_SIZE;

    // Step 5: Increment the occupancy count.
    buf->count++;

    // ── CRITICAL SECTION END ─────────────────────────────────
    pthread_mutex_unlock(&buf->mutex);

    // Step 7: Signal that one more filled slot is available.
    // This wakes up a validator that was blocked on sem_wait(&sem_filled).
    sem_post(&buf->sem_filled);
}

// ============================================================
//  shm_buffer_consume()
//  Called by a Validator thread to retrieve a transaction.
//
//  THE PROTOCOL (mirror image of produce):
//    1. sem_wait(sem_filled)  — wait for a filled slot (blocks if empty)
//    2. mutex_lock()          — enter critical section
//    3. read data[head]       — copy transaction out of the slot
//    4. advance head          — move read position forward (wrap if needed)
//    5. decrement count       — update occupancy counter
//    6. mutex_unlock()        — leave critical section
//    7. sem_post(sem_empty)   — signal that one more slot is now free
// ============================================================
Transaction shm_buffer_consume(SharedMemoryBuffer* buf) {

    // Step 1: Wait for a filled slot.
    // Blocks if buffer is empty (sem_filled == 0).
    sem_wait(&buf->sem_filled);

    // Step 2: Lock mutex.
    pthread_mutex_lock(&buf->mutex);

    // ── CRITICAL SECTION START ────────────────────────────────

    // Step 3: Copy the transaction from the head slot.
    // We copy by value — the slot in the buffer is left with stale data,
    // but it will be overwritten before anyone reads it again
    // (sem_empty ensures a producer claims the slot before writing).
    Transaction txn = buf->data[buf->head];

    // Step 4: Advance head with wrap-around.
    buf->head = (buf->head + 1) % SHARED_BUFFER_SIZE;

    // Step 5: Decrement count.
    buf->count--;

    // ── CRITICAL SECTION END ─────────────────────────────────
    pthread_mutex_unlock(&buf->mutex);

    // Step 7: Signal that one more empty slot is available.
    // Wakes up a producer that was blocked because the buffer was full.
    sem_post(&buf->sem_empty);

    return txn;
}

// ============================================================
//  shm_buffer_count()
//  Returns the current item count. Thread-safe read.
// ============================================================
int shm_buffer_count(SharedMemoryBuffer* buf) {
    pthread_mutex_lock(&buf->mutex);
    int c = buf->count;
    pthread_mutex_unlock(&buf->mutex);
    return c;
}