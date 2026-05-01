// ============================================================
//  logger.cpp
//  DEDICATED LOGGER THREAD — Updated with rich UI formatting
//
//  Now uses ui_format_log() to produce colored, symbol-tagged
//  log lines instead of plain text.
// ============================================================

#include "logger.h"
#include "ui.h"

#include <queue>
#include <pthread.h>
#include <semaphore.h>
#include <cstdio>
#include <cstring>
#include <ctime>

// ── Module-level state ───────────────────────────────────────
static std::queue<LogMessage> g_log_queue;
static pthread_mutex_t        g_queue_mutex;
static sem_t                  g_queue_sem;
static pthread_t              g_logger_thread;

// ── Format time ──────────────────────────────────────────────
static void format_time(time_t t, char* buf, int buf_size) {
    struct tm* tm_info = localtime(&t);
    strftime(buf, buf_size, "%H:%M:%S", tm_info);
}

// ── Thread type to string ────────────────────────────────────
static const char* type_str(ThreadType t) {
    switch (t) {
        case ThreadType::PRODUCER:  return "PRODUCER";
        case ThreadType::VALIDATOR: return "VALIDATOR";
        case ThreadType::UPDATER:   return "UPDATER";
        case ThreadType::MONITOR:   return "MONITOR";
        case ThreadType::SYSTEM:    return "SYSTEM";
        default:                    return "UNKNOWN";
    }
}

// ── Print one message ─────────────────────────────────────────
static void print_message(const LogMessage& msg) {
    char time_buf[16];
    format_time(msg.timestamp, time_buf, sizeof(time_buf));

    // Use the UI formatter for colored output
    std::string line = ui_format_log(
        type_str(msg.thread_type),
        msg.thread_num,
        msg.text.c_str(),
        time_buf
    );

    printf("%s\n", line.c_str());
    fflush(stdout);
}

// ── Logger thread ─────────────────────────────────────────────
static void* logger_thread_func(void*) {
    // Startup line using UI formatting
    char now_buf[16];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    strftime(now_buf, sizeof(now_buf), "%H:%M:%S", tm_info);

    std::string start_line = ui_format_log(
        "SYSTEM", 0,
        "Logger thread started — rich terminal UI active.",
        now_buf
    );
    printf("%s\n", start_line.c_str());
    fflush(stdout);

    while (true) {
        sem_wait(&g_queue_sem);

        pthread_mutex_lock(&g_queue_mutex);
        LogMessage msg = g_log_queue.front();
        g_log_queue.pop();
        pthread_mutex_unlock(&g_queue_mutex);

        if (msg.shutdown) {
            // Drain remaining messages
            pthread_mutex_lock(&g_queue_mutex);
            while (!g_log_queue.empty()) {
                LogMessage rem = g_log_queue.front();
                g_log_queue.pop();
                pthread_mutex_unlock(&g_queue_mutex);
                if (!rem.shutdown) print_message(rem);
                pthread_mutex_lock(&g_queue_mutex);
            }
            pthread_mutex_unlock(&g_queue_mutex);

            char flush_buf[16];
            time_t fn = time(nullptr);
            struct tm* fti = localtime(&fn);
            strftime(flush_buf, sizeof(flush_buf), "%H:%M:%S", fti);
            std::string done_line = ui_format_log(
                "SYSTEM", 0,
                "Logger flushed. All messages delivered.",
                flush_buf
            );
            printf("%s\n", done_line.c_str());
            fflush(stdout);
            break;
        }

        print_message(msg);
    }

    return nullptr;
}

// ── Public API ────────────────────────────────────────────────
void logger_init() {
    pthread_mutex_init(&g_queue_mutex, nullptr);
    sem_init(&g_queue_sem, 0, 0);
    pthread_create(&g_logger_thread, nullptr, logger_thread_func, nullptr);
}

void logger_log(ThreadType type, int thread_num, const std::string& msg) {
    LogMessage lm(type, thread_num, msg);
    pthread_mutex_lock(&g_queue_mutex);
    g_log_queue.push(lm);
    pthread_mutex_unlock(&g_queue_mutex);
    sem_post(&g_queue_sem);
}

void logger_shutdown() {
    LogMessage sentinel;
    pthread_mutex_lock(&g_queue_mutex);
    g_log_queue.push(sentinel);
    pthread_mutex_unlock(&g_queue_mutex);
    sem_post(&g_queue_sem);
    pthread_join(g_logger_thread, nullptr);
    pthread_mutex_destroy(&g_queue_mutex);
    sem_destroy(&g_queue_sem);
}