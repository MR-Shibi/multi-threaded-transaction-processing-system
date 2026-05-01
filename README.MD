# Multi-Threaded Transaction Processing System

> **OS Project — C++17 · POSIX Threads · Shared Memory · Named Pipes · SQLite3**

A production-quality simulation of a banking transaction pipeline built entirely with low-level OS primitives. Every transaction travels through three thread stages connected by two different IPC mechanisms — demonstrating the Producer-Consumer problem, semaphore synchronization, mutex locks, and persistent database storage in a single working system.

---

## Pipeline Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   PRODUCER THREADS                          │
│          SLOW  ·  FAST  ·  BURST  ·  MANUAL INPUT          │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
         ┌───────────────────────────────┐
         │   POSIX SHARED MEMORY         │
         │   /dev/shm/txn_shared_buffer  │
         │   Circular buffer · 8 slots   │
         │   sem_empty + sem_filled      │
         │   + pthread_mutex             │
         └───────────────┬───────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                  VALIDATOR THREADS                          │
│     Session check → Balance check → SQL query builder       │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
         ┌───────────────────────────────┐
         │   NAMED PIPE (FIFO)           │
         │   /tmp/txn_query_pipe         │
         │   512-byte fixed-size msgs    │
         │   Atomic kernel-buffered      │
         └───────────────┬───────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                 DB UPDATER THREADS                          │
│         BEGIN · INSERT · UPDATE · COMMIT                    │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
         ┌───────────────────────────────┐
         │   SQLite3 (WAL mode)          │
         │   transactions.db             │
         │   4 tables · mutex protected  │
         └───────────────────────────────┘

   Supporting: Logger Thread (sole stdout owner)
               Monitor Thread (live snapshots every 2s)
```

---

## Features

- **Two-stage Producer-Consumer pipeline** using different IPC mechanisms at each stage
- **POSIX Shared Memory** (`shm_open` + `mmap`) for the structured Transaction buffer
- **Named Pipe / FIFO** (`mkfifo`) for streaming pre-built SQL query strings
- **Counting semaphores** prevent buffer overflow and underflow without busy-waiting
- **Mutex locks** protect all shared state (buffer, database connection, log queue)
- **Three automatic producer modes** — SLOW (800ms), FAST (200ms), BURST (4 rapid then pause)
- **Manual input mode** — type transactions live while the system runs
- **Session & balance validation** — user 5 always rejected (no session); insufficient funds rejected
- **Atomic database transactions** — `BEGIN; INSERT + UPDATE; COMMIT` ensures consistency
- **WAL mode SQLite** — readers never blocked by concurrent writers
- **Dedicated logger thread** — sole owner of stdout, async non-blocking queue
- **Live monitor panels** — bordered snapshots every 2 seconds with buffer occupancy bar
- **Signal handling** — `Ctrl+C` triggers fully ordered graceful shutdown
- **Rich terminal UI** — ANSI colors, box-drawing characters, per-thread color identity
- **1-second transition delays** — each pipeline stage change is clearly visible

---

## OS Concepts Demonstrated

| Concept | Implementation |
|---|---|
| POSIX Threads | `pthread_create`, `pthread_join` for all 9 thread types |
| Semaphores | `sem_init`, `sem_wait`, `sem_post` — counting semaphores for buffer slots |
| Mutex Locks | `pthread_mutex_lock` protecting buffer, database, log queue |
| Shared Memory | `shm_open`, `ftruncate`, `mmap`, `shm_unlink` |
| Named Pipes | `mkfifo`, `open`, `write`, `read`, `unlink` |
| Atomic Operations | `std::atomic<bool>` for shutdown flag, `std::atomic<int>` for transaction IDs |
| Signal Handling | `sigaction` for `SIGINT`/`SIGTERM`, async-signal-safe handler |
| Blocking I/O | `sem_wait` and pipe `read` sleep the thread — zero busy-waiting |
| WAL Mode | SQLite Write-Ahead Logging for concurrent read-while-write |
| Producer-Consumer | Two-stage pipeline — each validator is both consumer and producer |

---

## Project Structure

```
txn_system/
├── main.cpp              # Entry point: signals, CLI args, thread lifecycle
├── transaction.h         # Core Transaction struct — travels the full pipeline
├── shared_buffer.h/.cpp  # POSIX Shared Memory circular queue (Buffer 1)
├── fifo_queue.h/.cpp     # Named Pipe FIFO SQL channel (Buffer 2)
├── logger.h/.cpp         # Dedicated logger thread — async queue → stdout
├── ui.h/.cpp             # Terminal UI: ANSI colors, panels, input handling
├── database.h/.cpp       # SQLite3 layer: WAL mode, mutex, prepared statements
├── producer.h/.cpp       # SLOW/FAST/BURST auto + manual keyboard input
├── validator.h/.cpp      # Session check → balance check → SQL builder
├── updater.h/.cpp        # BEGIN/COMMIT SQL executor, EOF-driven shutdown
├── monitor.h/.cpp        # Read-only periodic system observer
├── Makefile              # Build, run, clean, db targets
├── .gitignore            # Excludes binaries, .db files, object files
├── LICENSE               # MIT License
└── README.md             # This file
```

---

## Requirements

```bash
# Ubuntu / WSL
sudo apt-get install g++ make libsqlite3-dev sqlite3
```

| Dependency | Purpose |
|---|---|
| `g++` (C++17) | Compiler |
| `libpthread` | POSIX threads, semaphores, mutexes |
| `librt` | `shm_open` on some Linux versions |
| `libsqlite3-dev` | SQLite3 embedded database |

---

## Build & Run

```bash
# Clone the repo
git clone https://github.com/YOUR_USERNAME/txn_system.git
cd txn_system

# Build
make

# Run (automatic + manual input)
./txn_system

# Run automatic producers only
./txn_system --auto

# Run manual keyboard input only
./txn_system --manual

# Stop gracefully
Ctrl+C
```

---

## Running Modes

### Automatic Mode (`--auto`)
Three producer threads generate transactions continuously at different speeds:
- **SLOW** — one transaction every 800ms, simulates light load
- **FAST** — one transaction every 200ms, simulates heavy load
- **BURST** — 4 rapid transactions then 2-second pause, simulates batch processing

### Manual Mode (`--manual`)
You type transactions at the `txn>` prompt while the system runs:
```
txn> 3 250 WITHDRAWAL     # user 3, $250, withdrawal
txn> 2 500 DEPOSIT        # user 2, $500, deposit
txn> 5 100 TRANSFER       # user 5 — REJECTED (no active session)
txn> quit                 # stop manual input
```

### Both Modes (default)
Automatic producers run in the background while you inject manual transactions.

---

## Test Scenarios

| Input | Expected Result |
|---|---|
| `5 200 WITHDRAWAL` | **REJECTED** — user 5 has no active session |
| `2 9999 WITHDRAWAL` | **REJECTED** — insufficient funds (balance $2200) |
| `3 100 DEPOSIT` | **VALID** → committed to SQLite |
| `1 500 TRANSFER` | **VALID** → committed, balance updated |

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
    expires_at INTEGER NOT NULL   -- Unix timestamp
);

-- Audit log: every transaction that entered the system
CREATE TABLE raw_transactions (
    txn_id     INTEGER PRIMARY KEY,
    user_id    INTEGER NOT NULL,
    amount     REAL    NOT NULL,
    type       TEXT    NOT NULL,
    status     TEXT    NOT NULL DEFAULT 'PENDING',
    created_at INTEGER NOT NULL
);
-- status: PENDING → PROCESSING → DONE | REJECTED

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

### Seeded Users

| ID | Name | Balance | Session |
|---|---|---|---|
| 1 | Alice | $1,500 | ✅ Active |
| 2 | Bob | $2,200 | ✅ Active |
| 3 | Charlie | $800 | ✅ Active |
| 4 | Diana | $3,100 | ✅ Active |
| 5 | Eve | $500 | ❌ Logged out |

---

## Inspect Results

```bash
# Open database after a run
make db

# Useful queries
SELECT user_id, name, balance FROM users;
SELECT status, COUNT(*) FROM raw_transactions GROUP BY status;
SELECT txn_id, user_id, amount, type, balance_after FROM transactions;
```

---

## Makefile Targets

```bash
make              # build the binary
make run          # build and run (--both mode)
make auto         # build and run (--auto mode)
make manual       # build and run (--manual mode)
make db           # open SQLite shell on transactions.db
make clean        # remove compiled files, database, runtime files
make help         # show all targets
```

---

## Shutdown Order

The graceful shutdown sequence is strictly ordered to prevent data loss:

```
Ctrl+C
  → signal_handler: g_running = false
  → fclose(stdin)         unblocks manual producer
  → join producers        all transactions enqueued
  → join validators       all queries written to pipe
  → close write_fd        EOF signal to updaters
  → join updaters         all queries committed to SQLite
  → join monitor          final snapshot printed
  → logger_shutdown()     queue drained, logger exits
  → shm_buffer_destroy()  shared memory unlinked
  → fifo_destroy()        named pipe removed
  → db_close()            SQLite connection closed
  → print_final_report()  summary printed to terminal
```

---

## Course Information

- **Course:** Operating Systems
- **Institution:** IQRA University
- **Instructor:** Sir Minhal Raza
- **Students:** Huzaifa Ahmed Bari (24k-0847) · Shoaib Hayat (24k-1028)

---

## License

MIT License — see [LICENSE](LICENSE) for details.