#ifndef MONITOR_H
#define MONITOR_H

#include "shared_buffer.h"
#include <pthread.h>
#include <csignal>

static const int MONITOR_INTERVAL_SEC = 2;

extern volatile sig_atomic_t g_running;

extern int g_next_txn_id;
extern pthread_mutex_t g_txn_id_mutex;

extern bool g_input_active;
extern pthread_mutex_t g_input_mutex;


// Structure to pass shared resources and identification to the monitor thread.
struct MonitorArgs {
    SharedMemoryBuffer* buffer;
    int                 thread_id;
};

// Thread function that periodically gathers system statistics from the database and shared buffer.
void* monitor_thread(void* args);

#endif // MONITOR_H