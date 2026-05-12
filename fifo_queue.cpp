#include "fifo_queue.h"
#include "logger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <errno.h>
#include <string>
#include <pthread.h>
#include <semaphore.h>

static pthread_mutex_t g_fifo_write_mutex; // Explicitly protects multiple writers from interleaving.
static pthread_mutex_t g_fifo_read_mutex;  // Explicitly protects multiple readers from partial reads.
static sem_t           g_fifo_sem_empty;   // Explicitly tracks available capacity in the pipe (showcase only).
static sem_t           g_fifo_sem_filled;  // Explicitly tracks available messages in the pipe (showcase only).

static const int PIPE_CAPACITY_SLOTS = 120; // Standard 64KB pipe / 512B messages


// Writes a fixed-size message to the pipe; uses a mutex and semaphores to explicitly demonstrate the producer-consumer pattern.
void fifo_write_query(int write_fd, const char* query) {
    char msg[FIFO_MSG_SIZE];
    memset(msg, 0, FIFO_MSG_SIZE);
    strncpy(msg, query, FIFO_MSG_SIZE - 1);

    sem_wait(&g_fifo_sem_empty);      // Explicitly wait for capacity.
    pthread_mutex_lock(&g_fifo_write_mutex); // Explicitly lock for atomic write.

    int total_written = 0;
    while (total_written < FIFO_MSG_SIZE) {
        int n = write(write_fd, msg + total_written, FIFO_MSG_SIZE - total_written);
        if (n <= 0) break;
        total_written += n;
    }

    pthread_mutex_unlock(&g_fifo_write_mutex);

    if (total_written == FIFO_MSG_SIZE) {
        sem_post(&g_fifo_sem_filled);
    } else {
        sem_post(&g_fifo_sem_empty);
        logger_log(ThreadType::SYSTEM, 0,
                   "[FIFO] Incomplete write (" + std::to_string(total_written) +
                       "/" + std::to_string(FIFO_MSG_SIZE) + ") — slot restored.");
    }
}

// Reads a message from the pipe; uses a mutex and semaphores to explicitly demonstrate thread-safe communication for showcase purposes.
bool fifo_read_query(int read_fd, char* query_out) {
    sem_wait(&g_fifo_sem_filled);     // Explicitly wait for data.
    pthread_mutex_lock(&g_fifo_read_mutex); // Explicitly lock for atomic read.

    int total_read = 0;
    bool success = true;
    while (total_read < FIFO_MSG_SIZE) {
        int n = read(read_fd, query_out + total_read, FIFO_MSG_SIZE - total_read);
        if (n <= 0) {
            success = false;
            break;
        }
        total_read += n;
    }

    pthread_mutex_unlock(&g_fifo_read_mutex);
    sem_post(&g_fifo_sem_empty);     // Explicitly signal capacity available.
    return success;
}


// Creates a named pipe (FIFO) for inter-process or inter-thread communication, establishing a channel for data exchange.
void fifo_create() {
    int result = mkfifo(FIFO_PATH, 0600);
    if (result == -1) {
        if (errno == EEXIST) {
            logger_log(ThreadType::SYSTEM, 0, "[FIFO] Named pipe already exists — reusing it.");
        } else {
            perror("[FIFO] mkfifo failed");
            throw std::runtime_error("mkfifo failed");
        }
    } else {
        logger_log(ThreadType::SYSTEM, 0, "[FIFO] Named pipe created at " + std::string(FIFO_PATH));
    }

    // Explicitly initializing showcase synchronization primitives.
    pthread_mutex_init(&g_fifo_write_mutex, nullptr);
    pthread_mutex_init(&g_fifo_read_mutex, nullptr);
    sem_init(&g_fifo_sem_empty, 0, PIPE_CAPACITY_SLOTS);
    sem_init(&g_fifo_sem_filled, 0, 0);
}

// Removes the named pipe from the filesystem, cleaning up the OS-level communication resource.
void fifo_destroy() {
    if (unlink(FIFO_PATH) == 0) {
        logger_log(ThreadType::SYSTEM, 0, "[FIFO] Named pipe removed.");
    }

    // Explicitly cleaning up showcase synchronization primitives.
    pthread_mutex_destroy(&g_fifo_write_mutex);
    pthread_mutex_destroy(&g_fifo_read_mutex);
    sem_destroy(&g_fifo_sem_empty);
    sem_destroy(&g_fifo_sem_filled);
}

// Opens the pipe for writing; this call blocks until a reader opens the other end, acting as a synchronization point.
int fifo_open_write() {
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Writer opening pipe (waiting for reader)...");
    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd == -1) {
        throw std::runtime_error("fifo_open_write failed");
    }
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Writer connected.");
    return fd;
}

// Opens the pipe for reading; blocks until a writer connects, synchronizing the startup of communicating threads.
int fifo_open_read() {
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Reader opening pipe (waiting for writer)...");
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error("fifo_open_read failed");
    }
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Reader connected.");
    return fd;
}

// Closes the file descriptor, releasing the OS handle to the communication pipe.
void fifo_close(int fd) {
    if (fd >= 0) close(fd);
}