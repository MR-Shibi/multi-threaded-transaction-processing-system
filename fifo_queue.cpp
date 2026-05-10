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
}

void fifo_destroy() {
    if (unlink(FIFO_PATH) == 0) {
        logger_log(ThreadType::SYSTEM, 0, "[FIFO] Named pipe removed.");
    } else {
        // perror("[FIFO] unlink warning");
    }
}

int fifo_open_write() {
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Writer opening pipe (waiting for reader)...");
    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd == -1) {
        throw std::runtime_error("fifo_open_write failed");
    }
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Writer connected.");
    return fd;
}

int fifo_open_read() {
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Reader opening pipe (waiting for writer)...");
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error("fifo_open_read failed");
    }
    logger_log(ThreadType::SYSTEM, 0, "[FIFO] Reader connected.");
    return fd;
}

void fifo_write_query(int write_fd, const char* query) {
    char msg[FIFO_MSG_SIZE];
    memset(msg, 0, FIFO_MSG_SIZE);
    strncpy(msg, query, FIFO_MSG_SIZE - 1);

    int total_written = 0;
    while (total_written < FIFO_MSG_SIZE) {
        int n = write(write_fd, msg + total_written, FIFO_MSG_SIZE - total_written);
        if (n <= 0) return;
        total_written += n;
    }
}

bool fifo_read_query(int read_fd, char* query_out) {
    int total_read = 0;
    while (total_read < FIFO_MSG_SIZE) {
        int n = read(read_fd, query_out + total_read, FIFO_MSG_SIZE - total_read);
        if (n == 0) return false;
        if (n < 0) return false;
        total_read += n;
    }
    return true;
}

void fifo_close(int fd) {
    if (fd >= 0) close(fd);
}