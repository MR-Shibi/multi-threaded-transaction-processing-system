#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <ctime>

enum class ThreadType {
    PRODUCER,
    VALIDATOR,
    UPDATER,
    MONITOR,
    SYSTEM
};

// Encapsulates a log entry; used for asynchronous logging between worker threads and the dedicated logger thread.
struct LogMessage {
    ThreadType  thread_type;
    int         thread_num;
    std::string text;
    time_t      timestamp;
    bool        shutdown;

    LogMessage(ThreadType t, int n, const std::string& msg)
        : thread_type(t),
          thread_num(n),
          text(msg),
          timestamp(time(nullptr)),
          shutdown(false) {}

    LogMessage()
        : thread_type(ThreadType::SYSTEM),
          thread_num(0),
          text(""),
          timestamp(0),
          shutdown(true) {}
};

// Spawns the background logger thread and initializes its synchronization primitives.
void logger_init();
// Thread-safe function to enqueue a message for logging; uses a semaphore to notify the logger thread.
void logger_log(ThreadType type, int thread_num, const std::string& msg);
// Initiates graceful shutdown of the logger thread and releases OS synchronization resources.
void logger_shutdown();

#endif // LOGGER_H