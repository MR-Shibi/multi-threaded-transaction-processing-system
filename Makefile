# ============================================================
#  Makefile — Multi-Threaded Transaction Processing System
#  OS Project — Phase 10: Final Submission
#
#  Prerequisites (install once):
#    sudo apt-get install libsqlite3-dev sqlite3
#
#  Targets:
#    make          — build the binary
#    make run      — build and run (automatic + manual mode)
#    make auto     — build and run automatic mode only
#    make manual   — build and run manual mode only
#    make clean    — remove compiled files and database
#    make db       — open the database in sqlite3 shell
#    make help     — show this help
# ============================================================
# ============================================================
#  Makefile — Multi-Threaded Transaction Processing System
#  OS Project — Phase 10 Final + Rich Terminal UI
#
#  Prerequisites:
#    sudo apt-get install libsqlite3-dev sqlite3
#
#  Targets:
#    make          build
#    make run      build + run (auto + manual)
#    make auto     build + run automatic only
#    make manual   build + run manual only
#    make db       open sqlite3 shell
#    make clean    remove build artifacts
#    make help     show usage
# ============================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -pthread
TARGET   := txn_system
LDFLAGS  := -lpthread -lrt -lsqlite3

SRCS :=	main.cpp          \
	shared_buffer.cpp  \
	fifo_queue.cpp     \
	logger.cpp         \
	ui.cpp             \
	database.cpp       \
	producer.cpp       \
	validator.cpp      \
	updater.cpp        \
	monitor.cpp

OBJS := $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Build successful!"
	@echo "  Run:  ./$(TARGET)           (auto + manual)"
	@echo "  Run:  ./$(TARGET) --auto    (automatic only)"
	@echo "  Run:  ./$(TARGET) --manual  (manual input only)"
	@echo "  Stop: Ctrl+C               (graceful shutdown)"
	@echo ""

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(TARGET) --both

auto: all
	./$(TARGET) --auto

manual: all
	./$(TARGET) --manual

db:
	@echo "Opening transactions.db ..."
	sqlite3 transactions.db

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f transactions.db transactions.db-wal transactions.db-shm
	rm -f /tmp/txn_query_pipe
	rm -f /dev/shm/txn_shared_buffer
	@echo "  Cleaned."

help:
	@echo ""
	@echo "  make          build the project"
	@echo "  make run      build and run (--both mode)"
	@echo "  make auto     build and run (--auto mode)"
	@echo "  make manual   build and run (--manual mode)"
	@echo "  make db       open SQLite shell on transactions.db"
	@echo "  make clean    remove all compiled and runtime files"
	@echo ""

.PHONY: all run auto manual db clean help