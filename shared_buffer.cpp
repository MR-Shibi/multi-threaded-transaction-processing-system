#include "shared_buffer.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include "logger.h"

// Implements the producer logic: decrements 'empty' semaphore, locks mutex, inserts data, then increments 'filled' semaphore.
void shm_buffer_produce(SharedMemoryBuffer* buf, const Transaction& txn) {
    sem_wait(&buf->sem_empty);
    pthread_mutex_lock(&buf->mutex);

    buf->data[buf->tail] = txn;
    buf->tail = (buf->tail + 1) % SHARED_BUFFER_SIZE;
    buf->count++;

    pthread_mutex_unlock(&buf->mutex);
    sem_post(&buf->sem_filled);
}

// Implements the consumer logic: decrements 'filled' semaphore, locks mutex, removes data, then increments 'empty' semaphore.
Transaction shm_buffer_consume(SharedMemoryBuffer* buf) {
    sem_wait(&buf->sem_filled);
    pthread_mutex_lock(&buf->mutex);

    Transaction txn = buf->data[buf->head];
    buf->head = (buf->head + 1) % SHARED_BUFFER_SIZE;
    buf->count--;

    pthread_mutex_unlock(&buf->mutex);
    sem_post(&buf->sem_empty);

    return txn;
}

// Sets up the shared memory segment and initializes the embedded mutex and semaphores for process sharing.
SharedMemoryBuffer* shm_buffer_create() {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd == -1) {
        throw std::runtime_error("shm_open failed: could not create shared memory");
    }

    if (ftruncate(fd, sizeof(SharedMemoryBuffer)) == -1) {
        throw std::runtime_error("ftruncate failed: could not resize shared memory");
    }

    void* ptr = mmap(NULL,
                     sizeof(SharedMemoryBuffer),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     fd,
                     0);

    if (ptr == MAP_FAILED) {
        throw std::runtime_error("mmap failed: could not map shared memory");
    }

    close(fd);

    SharedMemoryBuffer* buf = static_cast<SharedMemoryBuffer*>(ptr);

    memset((void*)buf, 0, sizeof(SharedMemoryBuffer));

    if (sem_init(&buf->sem_empty, 1, SHARED_BUFFER_SIZE) != 0) {
        throw std::runtime_error("sem_init failed for sem_empty");
    }

    if (sem_init(&buf->sem_filled, 1, 0) != 0) {
        throw std::runtime_error("sem_init failed for sem_filled");
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    if (pthread_mutex_init(&buf->mutex, &attr) != 0) {
        throw std::runtime_error("pthread_mutex_init failed");
    }
    pthread_mutexattr_destroy(&attr);

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

// Connects to an already initialized shared memory region.
SharedMemoryBuffer* shm_buffer_attach() {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
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

// Finalizes and unlinks the shared memory resource from the system.
void shm_buffer_destroy(SharedMemoryBuffer* buf) {
    if (buf == nullptr) return;

    sem_destroy(&buf->sem_empty);
    sem_destroy(&buf->sem_filled);

    pthread_mutex_destroy(&buf->mutex);

    munmap(buf, sizeof(SharedMemoryBuffer));

    shm_unlink(SHM_NAME);

    logger_log(ThreadType::SYSTEM, 0, "Shared memory segment released and cleaned up.");
}

// Thread-safe query for the current buffer occupancy level.
int shm_buffer_count(SharedMemoryBuffer* buf) {
    pthread_mutex_lock(&buf->mutex);
    int c = buf->count;
    pthread_mutex_unlock(&buf->mutex);
    return c;
}