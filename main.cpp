// ============================================================
//  main.cpp
//  Multi-Threaded Transaction Processing System
//
//  Production-quality entry point for the complete pipeline.
//  Initializes UI, Database, Shared Memory, and FIFO.
//  Manages lifecycle of Producer, Validator, and Updater threads.
// ============================================================

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <signal.h>
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

// ── Global state ─────────────────────────────────────────────
std::atomic<bool> g_running(true);
std::atomic<int> g_next_txn_id(1);
std::atomic<bool> g_input_active(false);

// ============================================================
//  Signal handler — async-signal-safe only
// ============================================================
static void signal_handler(int /*signum*/) {
  // Print shutdown notice directly (cannot use printf/logger here)
  const char *msg = "\n\033[1m\033[93m  ⚠  Shutdown signal received — "
                    "stopping gracefully...\033[0m\n\n";
  write(STDOUT_FILENO, msg, strlen(msg));
  g_running.store(false);
}

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

// ============================================================
//  Open both FIFO ends simultaneously
// ============================================================
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

// ============================================================
//  MAIN
// ============================================================
int main(int argc, char *argv[]) {

  // ── Parse arguments ───────────────────────────────────────
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

  // ── Setup ─────────────────────────────────────────────────
  register_signals();
  srand(static_cast<unsigned>(time(nullptr)));

  // ── Setup ─────────────────────────────────────────────────
  ui_init();
  ui_update_header(auto_mode, manual_mode);

  // ── Logger ────────────────────────────────────────────────
  logger_init();
  usleep(50000);
  logger_log(ThreadType::SYSTEM, 0, "System initializing...");

  // ── Fresh database ────────────────────────────────────────
  remove("transactions.db");
  remove("transactions.db-wal");
  remove("transactions.db-shm");
  db_init();
  usleep(80000);

  // ── Shared memory buffer ──────────────────────────────────
  SharedMemoryBuffer *buf = shm_buffer_create();
  logger_log(ThreadType::SYSTEM, 0,
             "Shared memory ready  " + std::string(SHM_NAME) + "  [" +
                 std::to_string(SHARED_BUFFER_SIZE) + " slots]");

  // ── Named pipe ────────────────────────────────────────────
  fifo_create();
  int write_fd = -1;
  int read_fd = open_fifo_ends(&write_fd);
  logger_log(ThreadType::SYSTEM, 0,
             "Named pipe ready  " + std::string(FIFO_PATH));

  // ── Thread storage ────────────────────────────────────────
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

  // ── Start Monitor ─────────────────────────────────────────
  monitor_args = {buf, 1};
  pthread_create(&monitor_tid, nullptr, monitor_thread, &monitor_args);

  // ── Start DB Updaters ─────────────────────────────────────
  for (int i = 0; i < NUM_UPDATERS; i++) {
    upd_args[i] = {read_fd, i + 1};
    pthread_create(&upd_tids[i], nullptr, updater_thread, &upd_args[i]);
  }

  // ── Start Validators ──────────────────────────────────────
  for (int i = 0; i < NUM_VALIDATORS; i++) {
    val_args[i] = {buf, write_fd, i + 1};
    pthread_create(&val_tids[i], nullptr, validator_thread, &val_args[i]);
  }

  // ── Start Auto Producers ──────────────────────────────────
  if (auto_mode) {
    ProducerStyle styles[NUM_PRODUCERS] = {
        ProducerStyle::SLOW, ProducerStyle::FAST, ProducerStyle::BURST};
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      prod_args[i] = {buf, i + 1, styles[i], false};
      pthread_create(&prod_tids[i], nullptr, producer_thread, &prod_args[i]);
    }
  }

  // ── Start Manual Producer ─────────────────────────────────
  if (manual_mode) {
    // manual=true means "I am the only mode — quitting wizard shuts system
    // down" manual=false means "combined mode — quitting wizard just closes
    // wizard"
    bool manual_only = !auto_mode;
    manual_args = {buf, auto_mode ? 4 : 1, ProducerStyle::SLOW, manual_only};
    pthread_create(&manual_tid, nullptr, manual_producer_thread, &manual_args);
  }

  logger_log(ThreadType::SYSTEM, 0,
             "All threads active. Press Ctrl+C to stop.");

  // ── Wait for shutdown signal ──────────────────────────────
  while (g_running.load())
    usleep(100000);

  // ── Ordered graceful shutdown ─────────────────────────────
  logger_log(ThreadType::SYSTEM, 0, "Shutdown in progress...");

  if (manual_mode)
    fclose(stdin);

  // 1. Stop producers (they check g_running)
  if (auto_mode)
    for (int i = 0; i < NUM_PRODUCERS; i++)
      pthread_join(prod_tids[i], nullptr);
  if (manual_mode)
    pthread_join(manual_tid, nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Producers stopped.");

  // 2. Push one poison pill per validator into the shared buffer.
  //    Each validator is blocked on shm_buffer_consume(). The pill
  //    wakes it up and it exits immediately when it sees is_shutdown.
  //    Without this, validators deadlock in pthread_join forever.
  logger_log(ThreadType::SYSTEM, 0,
             "Sending shutdown signals to validators...");
  for (int i = 0; i < NUM_VALIDATORS; i++)
    shm_buffer_produce(buf, Transaction::make_shutdown_pill());

  // 3. Join validators
  for (int i = 0; i < NUM_VALIDATORS; i++)
    pthread_join(val_tids[i], nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Validators stopped. All queries in pipe.");

  // 4. Close write end → EOF signal to updaters
  fifo_close(write_fd);

  // 5. Join updaters
  for (int i = 0; i < NUM_UPDATERS; i++)
    pthread_join(upd_tids[i], nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Updaters stopped. All queries committed.");

  // 6. Join monitor
  pthread_join(monitor_tid, nullptr);
  logger_log(ThreadType::SYSTEM, 0, "Monitor stopped.");

  // ── Flush logger ──────────────────────────────────────────
  int generated = g_next_txn_id.load() - 1;
  logger_log(ThreadType::SYSTEM, 0,
             "Total transactions generated: " + std::to_string(generated));
  logger_log(ThreadType::SYSTEM, 0, "Shutdown complete.");

  usleep(200000);
  logger_shutdown();

  // ── Cleanup ───────────────────────────────────────────────
  shm_buffer_destroy(buf);
  fifo_destroy();
  ui_shutdown();

  // ── Final report ──────────────────────────────────────────
  int done = db_count_raw_by_status("DONE");
  int rejected = db_count_raw_by_status("REJECTED");
  int pending = db_count_raw_by_status("PENDING");
  int processing = db_count_raw_by_status("PROCESSING");
  int committed = db_count_committed();
  db_close();

  ui_show_final_report(generated, done, rejected, pending, processing,
                       committed);

  return 0;
}