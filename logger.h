#ifndef LOGGER_H
#define LOGGER_H

// ============================================================
//  logger.h
//  DEDICATED LOGGER THREAD
//
//  Architecture:
//    Any thread → logger_log() → [mutex-protected queue] → Logger Thread → terminal
//
//  Rules:
//    1. Only the Logger thread ever calls printf().
//    2. logger_log() is non-blocking: enqueue and return instantly.
//    3. Shutdown is signaled via a sentinel message in the queue.
//    4. All messages in the queue are flushed before the logger exits.
// ============================================================

#include <string>   // std::string
#include <ctime>    // time_t

// ── Thread type labels ───────────────────────────────────────
// Used to format each log line, e.g.: [12:04:33] PRODUCER-2 ...
enum class ThreadType {
    PRODUCER,
    VALIDATOR,
    UPDATER,
    MONITOR,
    SYSTEM      // for main() and infrastructure messages
};

// ── A single log message ─────────────────────────────────────
// This struct is what travels through the log queue.
// It is small by design — cheap to copy onto the queue.
struct LogMessage {
    ThreadType  thread_type;
    int         thread_num;   // e.g., 2 for PRODUCER-2
    std::string text;         // the message body
    time_t      timestamp;    // when logger_log() was called
    bool        shutdown;     // sentinel flag — set only by logger_shutdown()

    // Normal message constructor
    LogMessage(ThreadType t, int n, const std::string& msg)
        : thread_type(t),
          thread_num(n),
          text(msg),
          timestamp(time(nullptr)),
          shutdown(false) {}

    // Sentinel constructor — signals the logger thread to stop
    LogMessage()
        : thread_type(ThreadType::SYSTEM),
          thread_num(0),
          text(""),
          timestamp(0),
          shutdown(true) {}
};

// ── Public API ───────────────────────────────────────────────

// Creates the log queue, initializes synchronization primitives,
// and starts the dedicated logger thread. Call once at startup.
void logger_init();

// Enqueues a log message. Non-blocking — returns instantly.
// Safe to call from any thread at any time after logger_init().
void logger_log(ThreadType type, int thread_num, const std::string& msg);

// Sends a shutdown sentinel into the queue and waits for the
// logger thread to drain all remaining messages and exit.
// Call once at program shutdown, after all other threads have stopped.
void logger_shutdown();

#endif // LOGGER_H