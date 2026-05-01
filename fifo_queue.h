#ifndef FIFO_QUEUE_H
#define FIFO_QUEUE_H

// ============================================================
//  fifo_queue.h
//  BUFFER 2: Validator → DB Updater
//  Implemented using a Named Pipe / FIFO (mkfifo)
//
//  Validators write pre-built SQL query strings into the pipe.
//  DB Updater threads read those strings and execute them.
//
//  Why a pipe and not shared memory?
//    - We're passing variable-meaning but fixed-size strings.
//    - We don't need random access (no head/tail index needed).
//    - The kernel handles blocking and ordering automatically.
//    - Pipes are the natural OS primitive for streaming data.
//
//  Message protocol:
//    Every write = exactly FIFO_MSG_SIZE bytes (padded with \0).
//    Every read  = exactly FIFO_MSG_SIZE bytes.
//    This turns the raw byte stream into a clean message queue.
// ============================================================

// Path of the FIFO in the filesystem.
// mkfifo() creates a special file here. It looks like a regular
// file but behaves like a pipe — reading blocks until data arrives.
static constexpr const char* FIFO_PATH = "/tmp/txn_query_pipe";

// Fixed message size in bytes.
// All writes and reads use exactly this many bytes.
// Must be <= PIPE_BUF (4096 on Linux) for atomic writes.
static const int FIFO_MSG_SIZE = 512;

// ── Function declarations ────────────────────────────────────

// Creates the named pipe on disk. Idempotent — safe to call
// even if it already exists from a previous (crashed) run.
void fifo_create();

// Removes the named pipe file. Called at shutdown.
void fifo_destroy();

// Opens the FIFO for writing (used by validator threads).
// BLOCKS until at least one reader has opened the other end.
// Returns a file descriptor.
int fifo_open_write();

// Opens the FIFO for reading (used by DB Updater threads).
// BLOCKS until at least one writer has opened the other end.
// Returns a file descriptor.
int fifo_open_read();

// Writes one SQL query string through the pipe.
// Pads the message to exactly FIFO_MSG_SIZE bytes.
// Thread-safe: atomic because FIFO_MSG_SIZE < PIPE_BUF.
void fifo_write_query(int write_fd, const char* query);

// Reads one SQL query string from the pipe.
// BLOCKS if no data is available.
// Returns false when the pipe is closed (all writers gone = shutdown).
bool fifo_read_query(int read_fd, char* query_out);

// Closes a file descriptor opened with fifo_open_write/read.
void fifo_close(int fd);

#endif // FIFO_QUEUE_H