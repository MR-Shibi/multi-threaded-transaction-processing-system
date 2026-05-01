# Multi-Threaded Transaction Processing System

<div align="center">

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?style=for-the-badge&logo=cplusplus)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL-orange?style=for-the-badge&logo=linux)
![SQLite](https://img.shields.io/badge/SQLite-3-lightblue?style=for-the-badge&logo=sqlite)
![Threads](https://img.shields.io/badge/POSIX-Threads-green?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)

**OS Project — FAST University**
Huzaifa Ahmed Bari `24k-0847` · Shoaib Hayat `24k-1028`
Course: Operating Systems · Instructor: Sir Minhal Raza

</div>

---

## Overview

A complete, production-quality simulation of a multi-stage banking transaction pipeline built entirely with low-level OS primitives. Every transaction travels through **three thread stages** connected by **two different IPC mechanisms**, demonstrating the Producer-Consumer problem, semaphore synchronization, mutex locks, signal handling, and persistent database storage — all working together in one running system.

The system features a **rich terminal UI** with ANSI colors, bordered panels, per-thread color identity, and a **step-by-step guided wizard** for manual transaction input that never gets interrupted by background monitoring.

---

## Pipeline Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     PRODUCER THREADS                         │
│                                                              │
│   [AUTO]   SLOW (800ms) · FAST (200ms) · BURST (4 rapid)     │
│   [MANUAL] Step-by-step guided wizard input                  │
└────────────────────────┬─────────────────────────────────────┘
                         │  1 second transition delay
                         ▼
         ┌─────────────────────────────────┐
         │    POSIX SHARED MEMORY          │
         │  /dev/shm/txn_shared_buffer     │
         │                                 │
         │  Circular buffer · 8 slots      │
         │  sem_empty + sem_filled         │
         │  + pthread_mutex (process-shared│
         └────────────────┬────────────────┘
                          │  1 second transition delay
                          ▼
┌──────────────────────────────────────────────────────────────┐
│                    VALIDATOR THREADS (×2)                    │
│                                                              │
│   Session check → Balance check → SQL query builder          │
│   Marks: PENDING → PROCESSING → DONE | REJECTED              │
└────────────────────────┬─────────────────────────────────────┘
                         │  1 second transition delay
                         ▼
         ┌─────────────────────────────────┐
         │    NAMED PIPE (FIFO)            │
         │  /tmp/txn_query_pipe            │
         │                                 │
         │  512-byte fixed-size messages   │
         │  Atomic kernel-buffered writes  │
         │  Auto-blocking reads            │
         └────────────────┬────────────────┘
                          │  1 second transition delay
                          ▼
┌──────────────────────────────────────────────────────────────┐
│                  DB UPDATER THREADS (×2)                     │
│                                                              │
│   BEGIN · INSERT into transactions · UPDATE users · COMMIT   │
│   Logic-free executor — runs pre-built SQL from Validator    │
└────────────────────────┬─────────────────────────────────────┘
                         ▼
         ┌─────────────────────────────────┐
         │    SQLite3  (WAL mode)          │
         │  transactions.db                │
         │                                 │
         │  4 tables · mutex protected     │
         │  Concurrent reads allowed       │
         │  Crash-safe atomic commits      │
         └─────────────────────────────────┘

  Supporting Threads:
  ┌─────────────────────────────────────────────────────────┐
  │  LOGGER  — sole stdout owner · async non-blocking queue │
  │  MONITOR — read-only · 2s interval · live panel output  │
  │            pauses automatically during manual input     │
  └─────────────────────────────────────────────────────────┘
```

---

## Features

### Pipeline & IPC
- **Two-stage Producer-Consumer pipeline** using different IPC at each stage
- **POSIX Shared Memory** (`shm_open` + `mmap`) for structured Transaction buffer
- **Named Pipe / FIFO** (`mkfifo`) for streaming pre-built SQL query strings
- **Counting semaphores** prevent buffer overflow and underflow — zero busy-waiting
- **Mutex locks** protect all shared state (buffer, database connection, log queue)

### Producer Modes
- **SLOW** — one transaction every 800ms, simulates light load
- **FAST** — one transaction every 200ms, simulates heavy load
- **BURST** — 4 rapid transactions then 2-second pause, simulates batch
- **MANUAL** — step-by-step guided wizard with numbered menus and validation

### Validation
- **Session check** — user 5 (Eve) has no active session, always rejected
- **Balance check** — withdrawal/transfer exceeding balance is rejected
- **Pre-built SQL** — validator builds complete INSERT+UPDATE query, updater just executes

### Database
- **WAL mode SQLite** — concurrent readers never blocked by writers
- **Atomic transactions** — `BEGIN; INSERT + UPDATE; COMMIT` — all-or-nothing
- **4 tables** — `users`, `sessions`, `raw_transactions`, `transactions`
- **Full audit trail** — every transaction recorded from creation to commit or rejection

### Terminal UI
- **ANSI color system** — each thread type has a unique color identity
- **Bordered panels** — box-drawing characters for all major output
- **Step-by-step wizard** — numbered menus replace free-text input (no more typos)
- **Monitor panels** — live snapshots with ASCII buffer bar and throughput
- **1-second delays** — each pipeline stage transition is clearly visible
- **Non-interrupting monitor** — pauses automatically while user is typing

### System Quality
- **Signal handling** — `Ctrl+C` triggers fully ordered graceful shutdown
- **Ordered shutdown** — producers → validators → pipe close → updaters → monitor
- **Zero data loss** — every transaction in the pipeline is committed before exit
- **Command-line modes** — `--auto`, `--manual`, `--both`

---

## OS Concepts Demonstrated

| Concept | Where Used | API |
|---|---|---|
| POSIX Threads | All 9 thread types | `pthread_create`, `pthread_join` |
| Counting Semaphores | Shared memory buffer (empty + filled slots) | `sem_init`, `sem_wait`, `sem_post` |
| Mutex Locks | Buffer, database connection, log queue | `pthread_mutex_lock/unlock` |
| POSIX Shared Memory | Buffer 1 — Transaction struct storage | `shm_open`, `ftruncate`, `mmap`, `shm_unlink` |
| Named Pipes / FIFO | Buffer 2 — SQL query string channel | `mkfifo`, `open`, `write`, `read` |
| Atomic Operations | Shutdown flag, transaction ID counter, input flag | `std::atomic<bool/int>` |
| Signal Handling | Graceful Ctrl+C / kill shutdown | `sigaction`, `SA_RESTART` |
| Blocking I/O | `sem_wait` and pipe `read` sleep the thread | Zero busy-waiting |
| WAL Mode | Concurrent read-while-write in SQLite | `PRAGMA journal_mode=WAL` |
| Process-Shared Mutex | Mutex embedded in shared memory segment | `PTHREAD_PROCESS_SHARED` |
| Producer-Consumer | Two-stage pipeline, validator is both | Consumer of SHM + Producer of FIFO |

---

## Project Structure

```
txn_system/
│
├── main.cpp              Entry point: signals, CLI args, thread lifecycle,
│                         ordered graceful shutdown
│
├── transaction.h         Core Transaction struct — travels the full pipeline.
│                         Fields added by each stage without removing previous ones
│
├── shared_buffer.h       POSIX Shared Memory circular queue interface
├── shared_buffer.cpp     shm_open + mmap + semaphores + mutex implementation
│
├── fifo_queue.h          Named Pipe FIFO SQL channel interface
├── fifo_queue.cpp        mkfifo + fixed 512-byte message protocol
│
├── logger.h              Async logger thread interface
├── logger.cpp            Sole stdout owner — queue drained by dedicated thread
│
├── ui.h                  Terminal UI system interface
├── ui.cpp                ANSI colors, box-drawing, step-by-step wizard panels
│
├── database.h            SQLite3 layer interface
├── database.cpp          WAL mode, mutex protection, prepared statements
│
├── producer.h            Producer thread interface
├── producer.cpp          SLOW/FAST/BURST auto modes + guided wizard input
│
├── validator.h           Validator thread interface
├── validator.cpp         Session/balance check, SQL builder, FIFO writer
│
├── updater.h             DB Updater thread interface
├── updater.cpp           BEGIN/COMMIT executor, EOF-driven shutdown
│
├── monitor.h             Monitor thread interface (includes g_input_active flag)
├── monitor.cpp           Read-only observer, pauses during manual input
│
├── Makefile              Build, run, clean, db targets
├── README.md             This file
├── .gitignore            Excludes binaries, .db, .o files
├── LICENSE               MIT License
└── CONTRIBUTING.md       Contribution guide
```

---

## Requirements

```bash
# Ubuntu / WSL (one-time setup)
sudo apt-get install g++ make libsqlite3-dev sqlite3
```

| Dependency | Version | Purpose |
|---|---|---|
| `g++` | C++17 | Compiler |
| `libpthread` | POSIX | Threads, semaphores, mutexes |
| `librt` | POSIX | `shm_open` realtime extensions |
| `libsqlite3-dev` | 3.x | Embedded database |

---

## Build & Run

```bash
# 1. Clone
git clone https://github.com/MR-Shibi/multi-threaded-transaction-processing-system.git
cd multi-threaded-transaction-processing-system

# 2. Build
make

# 3. Run
./txn_system              # automatic + manual (default)
./txn_system --auto       # automatic producers only
./txn_system --manual     # guided wizard only
./txn_system --both       # both modes explicitly
./txn_system --help       # show usage

# 4. Stop
Ctrl+C                    # triggers graceful ordered shutdown
```

---

## Manual Mode — Step-by-Step Wizard

When running `--manual` or `--both`, the wizard guides you through each transaction:

**STEP 1 — Select User** (numbered list, no typing names)
```
╔═══════════ STEP 1 of 3 — Select User ════════════╗
║  [1]  Alice       $1,500         SESSION ACTIVE  ║
║  [2]  Bob         $2,200         SESSION ACTIVE  ║
║  [3]  Charlie     $800           SESSION ACTIVE  ║
║  [4]  Diana       $3,100         SESSION ACTIVE  ║
║  [5]  Eve         $500           NO SESSION      ║
╚══════════════════════════════════════════════════╝
Select user [1-5] or 'q' to quit:
```

**STEP 2 — Select Type** (numbered menu, no spelling errors possible)
```
╔══════════ STEP 2 of 3 — Transaction Type ════════╗
║              User: [3] Charlie                   ║
║  [1]  DEPOSIT      Add money to account          ║
║  [2]  WITHDRAWAL   Take money from account       ║
║  [3]  TRANSFER     Send money (deducts balance)  ║
╚══════════════════════════════════════════════════╝
Select type [1-3]:
```

**STEP 3 — Enter Amount** (shows balance and limit)
```
╔════════════ STEP 3 of 3 — Enter Amount ══════════╗
║  User: [3] Charlie   Type: WITHDRAWAL            ║
║  Current Balance: $800.00                        ║
║  Max you can enter: $800.00                      ║
╚══════════════════════════════════════════════════╝
Enter amount:
```

**STEP 4 — Confirm** then green **QUEUED** success panel.

> The monitor thread **automatically pauses** during every prompt so it never interrupts your input. Each step completes fully before the next one appears.

---

## Test Scenarios

| Input | Expected Result | Reason |
|---|---|---|
| User `5`, any type, any amount | **REJECTED** | Eve has no active session |
| User `2`, WITHDRAWAL, `$9999` | **REJECTED** | Bob's balance is $2,200 |
| User `1`, DEPOSIT, `$500` | **VALID → COMMITTED** | Alice has active session, deposits always pass |
| User `3`, WITHDRAWAL, `$100` | **VALID → COMMITTED** | Charlie has session, balance $800 > $100 |
| User `4`, TRANSFER, `$5000` | **REJECTED** | Diana's balance is $3,100 < $5,000 |

---

## Database Schema

```sql
-- Registered users with balances
CREATE TABLE users (
    user_id      INTEGER PRIMARY KEY,
    name         TEXT    NOT NULL,
    balance      REAL    NOT NULL DEFAULT 0.0,
    credit_limit REAL    NOT NULL DEFAULT 500.0
);

-- Active login sessions
CREATE TABLE sessions (
    session_id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(user_id),
    is_active  INTEGER NOT NULL DEFAULT 1,
    expires_at INTEGER NOT NULL        -- Unix timestamp
);

-- Audit log: EVERY transaction that entered Buffer 1
-- Status: PENDING → PROCESSING → DONE | REJECTED
CREATE TABLE raw_transactions (
    txn_id     INTEGER PRIMARY KEY,
    user_id    INTEGER NOT NULL,
    amount     REAL    NOT NULL,
    type       TEXT    NOT NULL,
    status     TEXT    NOT NULL DEFAULT 'PENDING',
    created_at INTEGER NOT NULL
);

-- Final record: only successfully committed transactions
CREATE TABLE transactions (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    txn_id        INTEGER NOT NULL,
    user_id       INTEGER NOT NULL,
    amount        REAL    NOT NULL,
    type          TEXT    NOT NULL,
    status        TEXT    NOT NULL DEFAULT 'PAID',
    balance_after REAL    NOT NULL,
    committed_at  INTEGER NOT NULL
);
```

### Seeded Test Users

| ID | Name | Balance | Session |
|---|---|---|---|
| 1 | Alice | $1,500 | ✅ Active |
| 2 | Bob | $2,200 | ✅ Active |
| 3 | Charlie | $800 | ✅ Active |
| 4 | Diana | $3,100 | ✅ Active |
| 5 | Eve | $500 | ❌ No session — always rejected |

---

## Makefile Targets

```bash
make              # build the binary
make run          # build and run (--both mode)
make auto         # build and run (--auto mode)
make manual       # build and run (--manual mode)
make db           # open SQLite3 shell on transactions.db
make clean        # remove all compiled files, database, runtime files
make help         # show all targets with descriptions
```

---

## Inspect Results

```bash
# After any run:
make db

# Inside sqlite3 shell:
SELECT user_id, name, balance FROM users;
SELECT status, COUNT(*) FROM raw_transactions GROUP BY status;
SELECT txn_id, user_id, amount, type, balance_after FROM transactions;
.quit
```

---

## Graceful Shutdown Sequence

The shutdown order is strictly enforced to guarantee zero data loss:

```
Ctrl+C
  ↓
signal_handler()              sets g_running = false  (async-signal-safe)
  ↓
fclose(stdin)                 unblocks manual wizard's fgets()
  ↓
pthread_join(producers)       wait — all transactions enqueued to SHM
  ↓
pthread_join(validators)      wait — all queries written to Named Pipe
  ↓
fifo_close(write_fd)          EOF signal sent to DB Updater threads
  ↓
pthread_join(updaters)        wait — all queries committed to SQLite
  ↓
pthread_join(monitor)         final snapshot printed
  ↓
logger_shutdown()             queue fully drained, logger thread exits
  ↓
shm_buffer_destroy()          munmap + shm_unlink — /dev/shm cleaned up
fifo_destroy()                unlink — /tmp/txn_query_pipe removed
db_close()                    sqlite3_close()
  ↓
print_final_report()          bordered summary with PASS/FAIL checks
```

> Reversing any step causes either deadlock, data loss, or a crash. The order above is the only correct sequence.

---

## Thread Color Identity

Each thread type has a permanent color in all log output:

| Thread | Color | Symbol |
|---|---|---|
| PRODUCER | Cyan | `>` |
| VALIDATOR | Yellow | `#` |
| UPDATER | Green | `+` |
| MONITOR | Magenta | `o` |
| SYSTEM | White | `.` |
| REJECTED messages | Red | — |
| VALID / COMMITTED | Green | — |

---

## Common Issues

| Problem | Cause | Fix |
|---|---|---|
| `make: *** No rule to make target` | `sqlite3_stub.c` missing | Run `sudo apt-get install libsqlite3-dev` |
| `fatal error: sqlite3.h not found` | Dev package not installed | `sudo apt-get install libsqlite3-dev` |
| `Permission denied` on `/dev/shm/` | Previous crash left segment | `rm /dev/shm/txn_shared_buffer` |
| `Address already in use` on pipe | Previous crash left FIFO | `rm /tmp/txn_query_pipe` |
| `make clean` then rebuild | Stale `.o` files | Always do `make clean && make` after file changes |

---

## License

MIT License — see [LICENSE](LICENSE) for full text.

---

## Course Information

| | |
|---|---|
| **University** | FAST University |
| **Course** | Operating Systems |
| **Instructor** | Sir Minhal Raza |
| **Student 1** | Huzaifa Ahmed Bari — Roll No: 24k-0847 |
| **Student 2** | Shoaib Hayat — Roll No: 24k-1028 |