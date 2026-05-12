#ifndef FIFO_QUEUE_H
#define FIFO_QUEUE_H

static constexpr const char* FIFO_PATH = "/tmp/txn_query_pipe";

static const int FIFO_MSG_SIZE = 512;
static constexpr const char* SHUTDOWN_QUERY = "EXIT_UPDATERS";

// Creates a named pipe (FIFO) for inter-process or inter-thread communication.
void fifo_create();
// Deletes the named pipe from the system.
void fifo_destroy();

// Opens the pipe for writing; blocks until a reader is connected.
int fifo_open_write();
// Opens the pipe for reading; blocks until a writer is connected.
int fifo_open_read();

// Sends a SQL query through the pipe; provides a thread-safe communication channel.
void fifo_write_query(int write_fd, const char* query);
// Reads a SQL query from the pipe; blocks if the pipe is empty.
bool fifo_read_query(int read_fd, char* query_out);

// Closes the file descriptor for the pipe.
void fifo_close(int fd);

#endif // FIFO_QUEUE_H