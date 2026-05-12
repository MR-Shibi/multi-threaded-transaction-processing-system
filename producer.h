#ifndef PRODUCER_H
#define PRODUCER_H

#include "shared_buffer.h"

enum class ProducerStyle {
    SLOW,
    FAST,
    BURST
};

// Arguments passed to producer threads to define their behavior and shared resources.
struct ProducerArgs {
    SharedMemoryBuffer* buffer;
    int                 thread_id;
    ProducerStyle       style;
    bool                manual;
};

// Entry point for automated producer threads; synchronizes with consumers via shared buffer semaphores.
void* producer_thread(void* args);
// Entry point for manual producer thread; captures keyboard input to generate transactions.
void* manual_producer_thread(void* args);

#endif // PRODUCER_H