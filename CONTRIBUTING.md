# Contributing

## Getting Started

```bash
git clone https://github.com/YOUR_USERNAME/txn_system.git
cd txn_system
sudo apt-get install libsqlite3-dev sqlite3
make
./txn_system --auto
```

## Project Layout

Each file has one responsibility. Before editing, understand which layer you are in:

| Layer | Files | Rule |
|---|---|---|
| IPC — Buffer 1 | `shared_buffer.h/.cpp` | No database calls allowed here |
| IPC — Buffer 2 | `fifo_queue.h/.cpp` | No database calls allowed here |
| Business logic | `validator.cpp` | The ONLY place that decides valid/rejected |
| DB execution | `updater.cpp` | No business logic — runs queries only |
| Presentation | `ui.cpp`, `logger.cpp` | The ONLY files that call `printf()` |

## Code Style

- C++17, no exceptions in thread functions
- Every thread function signature: `void* name(void* args)`
- All shared state accessed under a mutex or via atomics
- `logger_log()` instead of `printf()` in all thread files
- `snprintf` for all fixed-buffer string formatting

## Submitting Changes

1. Fork the repository
2. Create a branch: `git checkout -b feature/your-feature`
3. Make your changes and ensure `make` compiles with zero errors
4. Test with `./txn_system --auto` for at least 10 seconds
5. Open a Pull Request with a clear description