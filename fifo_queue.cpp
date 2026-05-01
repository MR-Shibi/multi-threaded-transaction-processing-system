// ============================================================
//  fifo_queue.cpp
//  BUFFER 2: Named Pipe Implementation
//
//  This file implements all FIFO operations used by Validator
//  threads (writers) and DB Updater threads (readers).
// ============================================================

#include "fifo_queue.h"

#include <sys/types.h>   // mode_t type
#include <sys/stat.h>    // mkfifo(), stat()
#include <fcntl.h>       // open(), O_WRONLY, O_RDONLY
#include <unistd.h>      // write(), read(), close(), unlink()
#include <cstring>       // memset(), strncpy()
#include <cstdio>        // printf(), perror()
#include <stdexcept>     // std::runtime_error
#include <errno.h>       // errno, EEXIST

// ============================================================
//  fifo_create()
//
//  mkfifo() creates a special file in the filesystem.
//  From the outside it looks like a file — you can see it with
//  `ls -l /tmp/txn_query_pipe` and it shows type 'p' (pipe).
//  But when you open() it, the kernel intercepts all reads and
//  writes and routes them through an internal pipe buffer.
//
//  The 0666 permission means read+write for owner, group, others.
// ============================================================
void fifo_create() {
    // Try to create the FIFO.
    int result = mkfifo(FIFO_PATH, 0666);

    if (result == -1) {
        if (errno == EEXIST) {
            // The FIFO already exists — probably a leftover from a
            // previous run that crashed before calling fifo_destroy().
            // This is fine. We can reuse it.
            printf("[FIFO] Named pipe already exists at %s — reusing it.\n",
                   FIFO_PATH);
        } else {
            // A real error (e.g., permission denied, bad path).
            // perror() prints the error string corresponding to errno.
            perror("[FIFO] mkfifo failed");
            throw std::runtime_error("mkfifo failed");
        }
    } else {
        printf("[FIFO] Named pipe created at %s\n", FIFO_PATH);
    }
}

// ============================================================
//  fifo_destroy()
//
//  unlink() deletes the FIFO file from the filesystem.
//  Any threads currently blocked reading/writing will get an
//  error on their next operation.
// ============================================================
void fifo_destroy() {
    if (unlink(FIFO_PATH) == 0) {
        printf("[FIFO] Named pipe removed: %s\n", FIFO_PATH);
    } else {
        // Not a fatal error if it was already removed.
        perror("[FIFO] unlink warning");
    }
}

// ============================================================
//  fifo_open_write()
//
//  Opens the FIFO for writing.
//
//  CRITICAL BEHAVIOR: This call BLOCKS until at least one
//  process/thread has opened the other end for reading.
//  This is a kernel guarantee — it prevents data loss.
//
//  In our system: validator threads call this. They will block
//  here until at least one DB Updater thread calls fifo_open_read().
//  This natural synchronization means validators can't start
//  sending queries before any updater is ready to receive them.
// ============================================================
int fifo_open_write() {
    printf("[FIFO] Writer opening pipe (will block until a reader connects)...\n");

    // O_WRONLY: open for writing only.
    // This call blocks here until a reader opens the other end.
    int fd = open(FIFO_PATH, O_WRONLY);

    if (fd == -1) {
        perror("[FIFO] open for writing failed");
        throw std::runtime_error("fifo_open_write failed");
    }

    printf("[FIFO] Writer connected (fd=%d)\n", fd);
    return fd;
}

// ============================================================
//  fifo_open_read()
//
//  Opens the FIFO for reading.
//
//  CRITICAL BEHAVIOR: This call BLOCKS until at least one
//  process/thread has opened the other end for writing.
//
//  In our system: DB Updater threads call this. They block
//  until at least one validator thread calls fifo_open_write().
//
//  STARTUP ORDER:
//  We must start the reader threads BEFORE the writer threads,
//  or start them in parallel. If the writer opens first and the
//  reader hasn't opened yet, the writer blocks. If both open
//  in parallel, they unblock each other as soon as both are ready.
//  We handle this by launching both in separate threads in main().
// ============================================================
int fifo_open_read() {
    printf("[FIFO] Reader opening pipe (will block until a writer connects)...\n");

    // O_RDONLY: open for reading only.
    int fd = open(FIFO_PATH, O_RDONLY);

    if (fd == -1) {
        perror("[FIFO] open for reading failed");
        throw std::runtime_error("fifo_open_read failed");
    }

    printf("[FIFO] Reader connected (fd=%d)\n", fd);
    return fd;
}

// ============================================================
//  fifo_write_query()
//
//  Sends one SQL query string through the pipe.
//
//  Protocol:
//    1. Create a fixed-size buffer of FIFO_MSG_SIZE bytes.
//    2. Zero it out (all null bytes = padding).
//    3. Copy the query string into the start of the buffer.
//    4. Write exactly FIFO_MSG_SIZE bytes to the pipe.
//
//  WHY FIXED SIZE?
//  The pipe is a raw byte stream. If we wrote:
//    write(fd, "INSERT...", 50)   ← 50 bytes
//    write(fd, "UPDATE...", 30)   ← 30 bytes
//  The reader would see 80 bytes total with no boundary.
//  It can't tell where one message ends and the next begins.
//
//  By always writing 512 bytes, the reader knows: every 512
//  bytes it reads = exactly one complete SQL query.
//
//  WHY IS THIS THREAD-SAFE WITHOUT A MUTEX?
//  Linux guarantees that writes <= PIPE_BUF (4096 bytes) are
//  atomic — the kernel will never interleave two such writes.
//  Since 512 < 4096, two validators writing simultaneously will
//  each get their 512 bytes inserted as one unbroken block.
// ============================================================
void fifo_write_query(int write_fd, const char* query) {
    // Step 1: Create a fixed-size message buffer, all zeros.
    char msg[FIFO_MSG_SIZE];
    memset(msg, 0, FIFO_MSG_SIZE);

    // Step 2: Copy the query string in. The remaining bytes stay \0.
    // FIFO_MSG_SIZE - 1 leaves room for a null terminator at the end.
    strncpy(msg, query, FIFO_MSG_SIZE - 1);

    // Step 3: Write exactly FIFO_MSG_SIZE bytes.
    // write() returns the number of bytes actually written.
    // We loop to handle partial writes (rare but possible).
    int total_written = 0;
    while (total_written < FIFO_MSG_SIZE) {
        int n = write(write_fd,
                      msg + total_written,
                      FIFO_MSG_SIZE - total_written);
        if (n <= 0) {
            perror("[FIFO] write failed");
            return;
        }
        total_written += n;
    }
}

// ============================================================
//  fifo_read_query()
//
//  Receives one SQL query string from the pipe.
//
//  BLOCKING BEHAVIOR:
//    - If no data is in the pipe, read() sleeps automatically.
//      The thread gives up the CPU and wakes when data arrives.
//    - No busy-waiting. No semaphores. The kernel handles it.
//
//  SHUTDOWN DETECTION:
//    When ALL writers close their end of the pipe, read() returns
//    0 (EOF). This is how DB Updater threads know the system is
//    shutting down — they see EOF and exit their loops.
//
//  Returns:
//    true  — a query was successfully read into query_out
//    false — pipe closed (EOF), no more queries will come
// ============================================================
bool fifo_read_query(int read_fd, char* query_out) {
    // Read exactly FIFO_MSG_SIZE bytes.
    // We loop because read() can return fewer bytes than requested
    // (partial read) if interrupted by a signal.
    int total_read = 0;

    while (total_read < FIFO_MSG_SIZE) {
        int n = read(read_fd,
                     query_out + total_read,
                     FIFO_MSG_SIZE - total_read);

        if (n == 0) {
            // EOF: all writer file descriptors have been closed.
            // This is our shutdown signal.
            return false;
        }

        if (n < 0) {
            perror("[FIFO] read failed");
            return false;
        }

        total_read += n;
    }

    // query_out now contains exactly FIFO_MSG_SIZE bytes.
    // The first bytes are the SQL string; the rest are \0 padding.
    // Since it's null-terminated, treating it as a C string works.
    return true;
}

// ============================================================
//  fifo_close()
//  Closes the file descriptor.
//  When ALL writers close, readers see EOF.
//  When ALL readers close, writers get SIGPIPE on next write.
// ============================================================
void fifo_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}