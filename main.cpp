// ============================================================
//  main.cpp
//  ENTRY POINT — Phase 1: Project Setup & Verification
//
//  In Phase 1, main.cpp does three things:
//    1. Prints a system architecture overview (proves the code
//       structure is correctly understood)
//    2. Creates a Transaction object and fills in some fields
//       (proves the struct is correct and compiles cleanly)
//    3. Prints the struct's size in memory (important for shared
//       memory calculations in Phase 2)
//
//  Later phases will replace this with real thread creation.
// ============================================================
// ============================================================
//  main.cpp — Phase 2: Shared Memory Buffer Test
//
//  This phase tests the shared memory circular buffer by
//  spawning 2 producer threads and 2 consumer threads.
//
//  Expected behavior:
//    - Producers generate transactions and write to shared memory
//    - Consumers read from shared memory and print what they got
//    - Semaphores automatically block producers when full
//      and block consumers when empty — no busy-waiting
// ============================================================
// ============================================================
//  main.cpp — Phase 3: Named Pipe (FIFO) Test
//
//  This phase tests BOTH IPC mechanisms together:
//    - Shared Memory (Buffer 1): tested in Phase 2
//    - Named Pipe   (Buffer 2): new this phase
//
//  We simulate the full pipeline for the first time:
//
//    "Pseudo-Producer" threads
//          ↓  [Named Pipe]
//    "Pseudo-Updater" threads
//
//  The pseudo-producers act like validators — they directly
//  write SQL query strings into the FIFO, skipping Buffer 1
//  for now (that connection happens in Phase 7).
//
//  THE OPEN-ORDER PROBLEM DEMONSTRATION:
//  Both writer and reader threads are started simultaneously.
//  Each blocks on open() until the other side connects.
//  They unblock each other — this is the handshake.
// ============================================================
// ============================================================
//  main.cpp — Phase 4: Logger Thread Test
//
//  This test launches 6 threads simultaneously (simulating
//  2 producers, 2 validators, 1 updater, 1 monitor) and has
//  them all call logger_log() as fast as they can.
//
//  WITHOUT the logger: output would be scrambled garbage.
//  WITH the logger:    every line is clean and complete.
//
//  We also verify all previous phases still work by keeping
//  a brief shared memory buffer test at the start.
// ============================================================
// ============================================================
//  main.cpp — Phase 5: SQLite Database Layer Test
//
//  Tests every function in database.h:
//    1. db_init()                  — open, create tables, seed data
//    2. db_get_balance()           — read a user's balance
//    3. db_is_session_active()     — check login status
//    4. db_insert_raw_transaction()— producer audit log write
//    5. db_update_raw_status()     — validator status update
//    6. db_execute()               — updater runs a raw SQL string
//    7. db_count_raw_by_status()   — monitor counts by status
//    8. db_count_committed()       — monitor counts committed
//
//  We also simulate a multi-threaded scenario: 3 threads
//  hit the database simultaneously to verify the mutex works.
// ============================================================
// ============================================================
//  main.cpp — Phase 6: Producer Threads Test
//
//  Starts 3 automatic producers + 1 manual producer.
//  A simple "drain thread" consumes from shared memory so
//  the buffer never fills up and producers never block forever.
//  (Real validators replace the drain thread in Phase 7.)
//
//  Run for 6 seconds, then gracefully shut down.
//  After shutdown, query the database to verify every
//  transaction was recorded in raw_transactions.
// ============================================================
// ============================================================
//  main.cpp — Phase 7: Validator Threads Test
//
//  First time ALL THREE IPC mechanisms work together:
//
//    Producer Threads
//         ↓  [POSIX Shared Memory — Buffer 1]
//    Validator Threads
//         ↓  [Named Pipe FIFO — Buffer 2]
//    Simple Pipe Reader (placeholder for DB Updater in Phase 8)
//
//  Thread layout:
//    3 automatic producers  (SLOW, FAST, BURST)
//    2 validator threads
//    1 pipe reader thread   (reads and prints queries from FIFO)
//    1 manual producer      (keyboard input)
//
//  Runs for 8 seconds then gracefully shuts down.
// ============================================================
// ============================================================
//  main.cpp — Phase 8: DB Updater Threads
//
//  THE COMPLETE PIPELINE (first time all stages are real):
//
//    Producer Threads  (3 auto + 1 manual)
//         ↓  [POSIX Shared Memory — circular buffer]
//    Validator Threads  (2 threads)
//         ↓  [Named Pipe FIFO — SQL query strings]
//    DB Updater Threads  (2 threads)   ← NEW THIS PHASE
//         ↓
//    SQLite Database
//         ↑  read by
//    Logger Thread + Monitor (Phase 9)
//
//  After this phase, for the first time:
//    - transactions table gets real committed rows
//    - users.balance is actually updated after each transaction
//    - The full audit trail (raw_transactions + transactions) works
//
//  Runs for 10 seconds then graceful shutdown.
// ============================================================
// ============================================================
//  main.cpp — Phase 9: Monitor Thread
//
//  Complete pipeline + live monitoring:
//
//    Producer Threads  (3 auto + 1 manual)
//         ↓  [POSIX Shared Memory]
//    Validator Threads  (2 threads)
//         ↓  [Named Pipe FIFO]
//    DB Updater Threads  (2 threads)
//         ↓
//    SQLite Database
//
//  Supporting threads:
//    Logger Thread   — single owner of stdout
//    Monitor Thread  — reads & reports system state every 2s
//
//  Runs for 12 seconds so at least 5 monitor snapshots appear.
// ============================================================
// ============================================================
//  main.cpp — Phase 10: Full System Integration
//
//  Production-quality entry point for the complete pipeline.
//
//  Usage:
//    ./txn_system              — automatic mode (default)
//    ./txn_system --auto       — automatic producers only
//    ./txn_system --manual     — manual input only (no auto producers)
//    ./txn_system --both       — auto producers + manual input (default)
//
//  Stop with Ctrl+C — signal handler ensures graceful shutdown.
//
//  COMPLETE PIPELINE:
//
//    Producer Threads  (up to 3 auto + 1 manual)
//         ↓  [POSIX Shared Memory — shm_open/mmap]
//    Validator Threads  (2 threads)
//         ↓  [Named Pipe FIFO — mkfifo]
//    DB Updater Threads  (2 threads)
//         ↓
//    SQLite Database  (transactions.db)
//
//  Supporting:
//    Logger Thread   — sole owner of stdout
//    Monitor Thread  — reads system state every 2s
// ============================================================
// ============================================================
//  main.cpp — Phase 10 Final with Rich Terminal UI
// ============================================================
// ============================================================
//  main.cpp — Phase 10 Final with Rich Terminal UI
// ============================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <atomic>
#include <string>

#include "transaction.h"
#include "shared_buffer.h"
#include "fifo_queue.h"
#include "database.h"
#include "logger.h"
#include "producer.h"
#include "validator.h"
#include "updater.h"
#include "monitor.h"
#include "ui.h"

// ── Global state ─────────────────────────────────────────────
std::atomic<bool> g_running(true);
std::atomic<int>  g_next_txn_id(1);
std::atomic<bool> g_input_active(false);

// ============================================================
//  Signal handler — async-signal-safe only
// ============================================================
static void signal_handler(int /*signum*/) {
    // Print shutdown notice directly (cannot use printf/logger here)
    const char* msg =
        "\n\033[1m\033[93m  ⚠  Shutdown signal received — "
        "stopping gracefully...\033[0m\n\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    g_running.store(false);
}

static void register_signals() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
}

// ============================================================
//  Open both FIFO ends simultaneously
// ============================================================
static int open_fifo_ends(int* write_fd_out) {
    int read_fd = -1;
    struct OA { int* fd; };
    OA oa = { &read_fd };

    pthread_t tid;
    pthread_create(&tid, nullptr, [](void* arg) -> void* {
        OA* a = static_cast<OA*>(arg);
        *a->fd = fifo_open_read();
        return nullptr;
    }, &oa);

    usleep(50000);
    *write_fd_out = fifo_open_write();
    pthread_join(tid, nullptr);
    return read_fd;
}

// ============================================================
//  MAIN
// ============================================================
int main(int argc, char* argv[]) {

    // ── Parse arguments ───────────────────────────────────────
    bool auto_mode   = true;
    bool manual_mode = true;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--auto")   == 0) { auto_mode=true;  manual_mode=false; }
        else if (strcmp(argv[i], "--manual") == 0) { auto_mode=false; manual_mode=true;  }
        else if (strcmp(argv[i], "--both")   == 0) { auto_mode=true;  manual_mode=true;  }
        else if (strcmp(argv[i], "--help")   == 0) {
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

    // ── Print architecture banner ─────────────────────────────
    ui_print_banner(auto_mode, manual_mode);

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
    SharedMemoryBuffer* buf = shm_buffer_create();
    logger_log(ThreadType::SYSTEM, 0,
        "Shared memory ready  " + std::string(SHM_NAME)
        + "  [" + std::to_string(SHARED_BUFFER_SIZE) + " slots]");

    // ── Named pipe ────────────────────────────────────────────
    fifo_create();
    int write_fd = -1;
    int read_fd  = open_fifo_ends(&write_fd);
    logger_log(ThreadType::SYSTEM, 0,
        "Named pipe ready  " + std::string(FIFO_PATH));

    // ── Thread storage ────────────────────────────────────────
    static const int NUM_PRODUCERS  = 3;
    static const int NUM_VALIDATORS = 2;
    static const int NUM_UPDATERS   = 2;

    pthread_t prod_tids[NUM_PRODUCERS];
    pthread_t manual_tid;
    pthread_t val_tids[NUM_VALIDATORS];
    pthread_t upd_tids[NUM_UPDATERS];
    pthread_t monitor_tid;

    ProducerArgs  prod_args[NUM_PRODUCERS];
    ProducerArgs  manual_args;
    ValidatorArgs val_args[NUM_VALIDATORS];
    UpdaterArgs   upd_args[NUM_UPDATERS];
    MonitorArgs   monitor_args;

    // ── Start Monitor ─────────────────────────────────────────
    monitor_args = { buf, 1 };
    pthread_create(&monitor_tid, nullptr, monitor_thread, &monitor_args);

    // ── Start DB Updaters ─────────────────────────────────────
    for (int i = 0; i < NUM_UPDATERS; i++) {
        upd_args[i] = { read_fd, i + 1 };
        pthread_create(&upd_tids[i], nullptr,
                       updater_thread, &upd_args[i]);
    }

    // ── Start Validators ──────────────────────────────────────
    for (int i = 0; i < NUM_VALIDATORS; i++) {
        val_args[i] = { buf, write_fd, i + 1 };
        pthread_create(&val_tids[i], nullptr,
                       validator_thread, &val_args[i]);
    }

    // ── Start Auto Producers ──────────────────────────────────
    if (auto_mode) {
        ProducerStyle styles[NUM_PRODUCERS] = {
            ProducerStyle::SLOW,
            ProducerStyle::FAST,
            ProducerStyle::BURST
        };
        for (int i = 0; i < NUM_PRODUCERS; i++) {
            prod_args[i] = { buf, i + 1, styles[i], false };
            pthread_create(&prod_tids[i], nullptr,
                           producer_thread, &prod_args[i]);
        }
    }

    // ── Start Manual Producer ─────────────────────────────────
    if (manual_mode) {
        manual_args = { buf, auto_mode ? 4 : 1,
                        ProducerStyle::SLOW, true };
        pthread_create(&manual_tid, nullptr,
                       manual_producer_thread, &manual_args);
    }

    logger_log(ThreadType::SYSTEM, 0,
        "All threads active. Press Ctrl+C to stop.");

    // ── Wait for shutdown signal ──────────────────────────────
    while (g_running.load()) pause();

    // ── Ordered graceful shutdown ─────────────────────────────
    logger_log(ThreadType::SYSTEM, 0, "Shutdown in progress...");

    if (manual_mode) fclose(stdin);

    // 1. Join producers
    if (auto_mode)
        for (int i = 0; i < NUM_PRODUCERS; i++)
            pthread_join(prod_tids[i], nullptr);
    if (manual_mode)
        pthread_join(manual_tid, nullptr);
    logger_log(ThreadType::SYSTEM, 0, "Producers stopped.");

    // 2. Join validators
    for (int i = 0; i < NUM_VALIDATORS; i++)
        pthread_join(val_tids[i], nullptr);
    logger_log(ThreadType::SYSTEM, 0,
               "Validators stopped. All queries in pipe.");

    // 3. Close write end → EOF signal to updaters
    fifo_close(write_fd);

    // 4. Join updaters
    for (int i = 0; i < NUM_UPDATERS; i++)
        pthread_join(upd_tids[i], nullptr);
    logger_log(ThreadType::SYSTEM, 0,
               "Updaters stopped. All queries committed.");

    // 5. Join monitor
    pthread_join(monitor_tid, nullptr);
    logger_log(ThreadType::SYSTEM, 0, "Monitor stopped.");

    // ── Flush logger ──────────────────────────────────────────
    int generated = g_next_txn_id.load() - 1;
    logger_log(ThreadType::SYSTEM, 0,
        "Total transactions generated: "
        + std::to_string(generated));
    logger_log(ThreadType::SYSTEM, 0, "Shutdown complete.");

    usleep(200000);
    logger_shutdown();

    // ── Cleanup ───────────────────────────────────────────────
    shm_buffer_destroy(buf);
    fifo_destroy();

    // ── Final report ──────────────────────────────────────────
    int done       = db_count_raw_by_status("DONE");
    int rejected   = db_count_raw_by_status("REJECTED");
    int pending    = db_count_raw_by_status("PENDING");
    int processing = db_count_raw_by_status("PROCESSING");
    int committed  = db_count_committed();
    db_close();

    ui_print_final_report(generated, done, rejected,
                          pending, processing, committed);

    return 0;
}