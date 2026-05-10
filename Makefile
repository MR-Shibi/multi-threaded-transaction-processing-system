# ============================================================
#  Makefile — Multi-Threaded Transaction Processing System
#  OS Project — Final Dashboard with ncurses
# ============================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -pthread -Wno-format-extra-args
TARGET   := txn_system

# LDFLAGS include pthread, rt (for shm), sqlite3, and ncurses
LDFLAGS  := -lpthread -lrt -lsqlite3 -lncursesw

SRCS := main.cpp          \
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
	@echo "\n  Build successful! Run: ./$(TARGET)\n"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(TARGET) --both

auto: all
	./$(TARGET) --auto

manual: all
	./$(TARGET) --manual

db:
	sqlite3 transactions.db

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f transactions.db transactions.db-wal transactions.db-shm
	rm -f /tmp/txn_query_pipe
	@echo "  Cleaned."

help:
	@echo "\n  make          build the project"
	@echo "  make run      build and run (--both mode)"
	@echo "  make auto     build and run (--auto mode)"
	@echo "  make manual   build and run (--manual mode)"
	@echo "  make db       open SQLite shell on transactions.db"
	@echo "  make clean    remove compiled and runtime files\n"

.PHONY: all run auto manual db clean help