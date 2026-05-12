#ifndef UPDATER_H
#define UPDATER_H

// Arguments passed to updater threads, defining communication channels and identity.
struct UpdaterArgs {
    int read_fd;
    int thread_id;
};

// Entry point for updater threads; synchronizes with validators via the FIFO named pipe.
void* updater_thread(void* args);

#endif // UPDATER_H