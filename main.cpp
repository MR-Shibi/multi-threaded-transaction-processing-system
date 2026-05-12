#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <csignal>
#include <string>
#include <unistd.h>

#include "database.h"
#include "fifo_queue.h"
#include "logger.h"
#include "monitor.h"
#include "producer.h"
#include "shared_buffer.h"
#include "Transaction.h"
#include "ui.h"
#include "updater.h"
#include "validator.h"

volatile sig_atomic_t g_running = 1;

int g_next_txn_id = 1;
pthread_mutex_t g_txn_id_mutex = PTHREAD_MUTEX_INITIALIZER;

bool g_input_active = false;
pthread_mutex_t g_input_mutex = PTHREAD_MUTEX_INITIALIZER;


// Handles OS signals (like SIGINT) to toggle an atomic flag for graceful shutdown.
static void signal_handler(int) {
  const char *msg = "\n\033[1m\033[93m  ⚠  Shutdown signal received — "
                    "stopping gracefully...\033[0m\n\n";
  write(STDOUT_FILENO, msg, strlen(msg));
  g_running = 0;
}

// Configures the process to catch specific OS signals, ensuring resources are cleaned up before exit.
static void register_signals() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);
}

// Synchronizes the opening of both ends of a named pipe using a helper thread to avoid blocking deadlocks.
static int open_fifo_ends(int *write_fd_out) {
  int read_fd = -1;
  struct OA {
    int *fd;
  };
  OA oa = {&read_fd};

  pthread_t tid;
  pthread_create(
      &tid, nullptr,
      [](void *arg) -> void * {
        OA *a = static_cast<OA *>(arg);
        *a->fd = fifo_open_read();
        return nullptr;
      },
      &oa);

  usleep(50000);
  *write_fd_out = fifo_open_write();
  pthread_join(tid, nullptr);
  return read_fd;
}

// The main process entry point; orchestrates thread creation, resource allocation, and final cleanup.
int main(int argc, char *argv[]) {
  bool auto_mode = true;
  bool manual_mode = true;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--auto") == 0) {
      auto_mode = true;
      manual_mode = false;
    } else if (strcmp(argv[i], "--manual") == 0) {
      auto_mode = false;
      manual_mode = true;
    } else if (strcmp(argv[i], "--both") == 0) {
      auto_mode = true;
      manual_mode = true;
    } else if (strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [--auto | --manual | --both]\n", argv[0]);
      printf("  --auto    3 automatic producers only\n");
      printf("  --manual  keyboard input only\n");
      printf("  --both    auto + manual (default)\n");
      return 0;
    } else {
      printf("Unknown argument: %s  (use --help)\n", argv[i]);
      return 1;
    }
  }

  register_signals();
  srand(static_cast<unsigned>(time(nullptr)));

  ui_init();
  ui_update_header(auto_mode, manual_mode);

  logger_init();
  usleep(50000);
  logger_log(ThreadType::SYSTEM, 0, "System initializing...");

  remove("transactions.db");
  remove("transactions.db-wal");
  remove("transactions.db-shm");
  db_init();
  usleep(80000);

  SharedMemoryBuffer *buf = shm_buffer_create();
  logger_log(ThreadType::SYSTEM, 0,
             "Shared memory ready  " + std::string(SHM_NAME) + "  [" +
                 std::to_string(SHARED_BUFFER_SIZE) + " slots]");

  fifo_create();
  int write_fd = -1;
  int read_fd = open_fifo_ends(&write_fd);
  logger_log(ThreadType::SYSTEM, 0,
             "Named pipe ready  " + std::string(FIFO_PATH));

  static const int NUM_PRODUCERS = 3;
  static const int NUM_VALIDATORS = 2;
  static const int NUM_UPDATERS = 2;

  pthread_t prod_tids[NUM_PRODUCERS];
  pthread_t manual_tid;
  pthread_t val_tids[NUM_VALIDATORS];
  pthread_t upd_tids[NUM_UPDATERS];
  pthread_t monitor_tid;

  ProducerArgs prod_args[NUM_PRODUCERS];
  ProducerArgs manual_args;
  ValidatorArgs val_args[NUM_VALIDATORS];
  UpdaterArgs upd_args[NUM_UPDATERS];
  MonitorArgs monitor_args;

  monitor_args = {buf, 1};
  pthread_create(&monitor_tid, nullptr, monitor_thread, &monitor_args);

  for (int i = 0; i < NUM_UPDATERS; i++) {
    upd_args[i] = {read_fd, i + 1};
    pthread_create(&upd_tids[i], nullptr, updater_thread, &upd_args[i]);
  }

  for (int i = 0; i < NUM_VALIDATORS; i++) {
    val_args[i] = {buf, write_fd, i + 1};
    pthread_create(&val_tids[i], nullptr, validator_thread, &val_args[i]);
  }

  if (auto_mode) {
    ProducerStyle styles[NUM_PRODUCERS] = {
        ProducerStyle::SLOW, ProducerStyle::FAST, ProducerStyle::BURST};
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      prod_args[i] = {buf, i + 1, styles[i], false};
      pthread_create(&prod_tids[i], nullptr, producer_thread, &prod_args[i]);
    }
  }

  if (manual_mode) {
    bool manual_only = !auto_mode;
    manual_args = {buf, auto_mode ? 4 : 1, ProducerStyle::SLOW, manual_only};
    pthread_create(&manual_tid, nullptr, manual_producer_thread, &manual_args);
  }

  logger_log(ThreadType::SYSTEM, 0,
             "All threads active. Press Ctrl+C to stop.");

  while (g_running) {
    usleep(100000);
  }

  logger_log(ThreadType::SYSTEM, 0, "Shutdown in progress...");

  if (manual_mode)
    fclose(stdin);

  if (auto_mode)
    for (int i = 0; i < NUM_PRODUCERS; i++)
      pthread_join(prod_tids[i], nullptr);
  if (manual_mode)
    pthread_join(manual_tid, nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Producers stopped.");

  logger_log(ThreadType::SYSTEM, 0,
             "Sending shutdown signals to validators...");
  for (int i = 0; i < NUM_VALIDATORS; i++)
    shm_buffer_produce(buf, Transaction::make_shutdown_pill());

  for (int i = 0; i < NUM_VALIDATORS; i++)
    pthread_join(val_tids[i], nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Validators stopped. All queries in pipe.");

  logger_log(ThreadType::SYSTEM, 0,
             "Sending shutdown signals to updaters...");
  for (int i = 0; i < NUM_UPDATERS; i++)
    fifo_write_query(write_fd, SHUTDOWN_QUERY);

  fifo_close(write_fd);

  for (int i = 0; i < NUM_UPDATERS; i++)
    pthread_join(upd_tids[i], nullptr);
  if (read_fd >= 0)
    close(read_fd);
  logger_log(ThreadType::SYSTEM, 0, "Updaters stopped. All queries committed.");

  pthread_join(monitor_tid, nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Monitor stopped.");

  pthread_mutex_lock(&g_txn_id_mutex);
  int generated = g_next_txn_id - 1;
  pthread_mutex_unlock(&g_txn_id_mutex);
  logger_log(ThreadType::SYSTEM, 0,
             "Total transactions generated: " + std::to_string(generated));
  logger_log(ThreadType::SYSTEM, 0, "Shutdown complete.");

  usleep(200000);
  logger_shutdown();

  shm_buffer_destroy(buf);
  fifo_destroy();
  ui_shutdown();

  int done = db_count_raw_by_status("DONE");
  int rejected = db_count_raw_by_status("REJECTED");
  int pending = db_count_raw_by_status("PENDING");
  int processing = db_count_raw_by_status("PROCESSING");
  int forwarded = db_count_raw_by_status("FORWARDED");
  int failed = db_count_raw_by_status("FAILED");
  int committed = db_count_committed();
  db_close();

  ui_show_final_report(generated, done, rejected, pending, processing, forwarded,
                       failed, committed);

  return 0;
}