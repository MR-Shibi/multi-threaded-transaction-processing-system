#include "producer.h"
#include "Transaction.h"
#include "database.h"
#include "logger.h"
#include "shared_buffer.h"
#include "ui.h"

#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <unistd.h>
#include <csignal>

extern volatile sig_atomic_t g_running;
extern int g_next_txn_id;
extern pthread_mutex_t g_txn_id_mutex;
extern bool g_input_active;
extern pthread_mutex_t g_input_mutex;


static const int AUTO_TRANSITION_DELAY_US = 100000;

static const struct {
  int id;
  const char *name;
  bool has_session;
} USERS[] = {
    {1, "Alice", true}, {2, "Bob", true},  {3, "Charlie", true},
    {4, "Diana", true}, {5, "Eve", false},
};
static const int NUM_USERS = 5;

static constexpr const char *TXN_TYPES[] = {"DEPOSIT", "WITHDRAWAL", "TRANSFER"};

// Maps user ID to a human-readable name for logging; uses a static read-only lookup table.
static constexpr const char *get_user_name(int uid) {
  for (int i = 0; i < NUM_USERS; i++)
    if (USERS[i].id == uid)
      return USERS[i].name;
  return "Unknown";
}


// Entry point for automated producer threads; synchronizes with consumers via the shared buffer's semaphores.
void *producer_thread(void *args) {
  ProducerArgs *a = static_cast<ProducerArgs *>(args);
  SharedMemoryBuffer *buf = a->buffer;
  int id = a->thread_id;
  ProducerStyle style = a->style;

  const char *sn = "?";
  int delay = 0;
  switch (style) {
  case ProducerStyle::SLOW:
    sn = "SLOW";
    delay = 2000000;
    break;
  case ProducerStyle::FAST:
    sn = "FAST";
    delay = 1000000;
    break;
  case ProducerStyle::BURST:
    sn = "BURST";
    delay = 300000;
    break;
  }

  logger_log(ThreadType::PRODUCER, id,
             std::string("Auto producer started  [") + sn + " mode]");
  ui_set_thread_status("PRODUCER", id,
                       (std::string(sn) + " - started").c_str());

  sqlite3* conn = db_open_connection();
  if (!conn) {
    logger_log(ThreadType::PRODUCER, id, "FATAL: Could not open DB connection.");
    return nullptr;
  }

  while (g_running) {

    if (style == ProducerStyle::BURST) {
      for (int b = 0; b < 4; b++) {
        if (!g_running) break;

        Transaction txn = make_transaction(conn, id);
        usleep(AUTO_TRANSITION_DELAY_US);
        shm_buffer_produce(buf, txn);
        char st[72];
        snprintf(st, 72, "[%s] Queued #%d", sn, txn.transaction_id);
        ui_set_thread_status("PRODUCER", id, st);
        ui_queue_push(txn.transaction_id, get_user_name(txn.user_id),
                       txn.transaction_type, txn.amount);
        logger_log(ThreadType::PRODUCER, id,
                   "Transaction #" + std::to_string(txn.transaction_id) +
                       " placed in queue");
        usleep(delay);
      }
      usleep(2000000);
    } else {
      Transaction txn = make_transaction(conn, id);
      usleep(AUTO_TRANSITION_DELAY_US);
      shm_buffer_produce(buf, txn);
      char st2[72];
      snprintf(st2, 72, "[%s] Queued #%d", sn, txn.transaction_id);
      ui_set_thread_status("PRODUCER", id, st2);
      ui_queue_push(txn.transaction_id, get_user_name(txn.user_id),
                    txn.transaction_type, txn.amount);
      logger_log(ThreadType::PRODUCER, id,
                 "Transaction #" + std::to_string(txn.transaction_id) +
                     " placed in queue");
      usleep(delay);
    }
  }

  db_close_connection(conn);

  logger_log(ThreadType::PRODUCER, id, "Auto producer stopped.");
  return nullptr;
}

// Entry point for the manual input thread; uses a producer-consumer pattern to feed the system from the keyboard.
void *manual_producer_thread(void *args) {
  ProducerArgs *a = static_cast<ProducerArgs *>(args);
  SharedMemoryBuffer *buf = a->buffer;
  int id = a->thread_id;
  bool manual_only = a->manual;

  logger_log(ThreadType::PRODUCER, id, "Manual mode: Press 'M' to start a transaction.");
  
  nodelay(stdscr, TRUE); 
  keypad(stdscr, TRUE);

  char line[128];
  sqlite3* conn = db_open_connection();
  if (!conn) {
    logger_log(ThreadType::PRODUCER, id, "FATAL: Manual thread could not open DB.");
    return nullptr;
  }

  while (g_running) {

    int ch = wgetch(stdscr);
    if (ch == 'm' || ch == 'M') {
        logger_log(ThreadType::PRODUCER, id, "Manual transaction triggered.");
        nodelay(stdscr, FALSE); 

        int user_idx = -1;
        while (true) {
            if (!g_running) break;

            ui_wizard_clear();
            ui_wizard_print(0, 0, "SELECT USER (1-5):", CP_HEADER);
            for (int i = 0; i < 5; i++) {
                char uline[64];
                snprintf(uline, 64, "[%d] %-10s", USERS[i].id, USERS[i].name);
                ui_wizard_print(1, i * 15, uline, USERS[i].has_session ? CP_SUCCESS : CP_ERROR);
            }
            if (!wizard_read_line("Choice (1-5 or q): ", line, sizeof(line)) || is_quit(line)) break;
            
            int val = atoi(line);
            if (val >= 1 && val <= 5) { user_idx = val - 1; break; }
            ui_wizard_print(3, 0, "INVALID USER! Please enter 1-5.", CP_ERROR);
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        }

        if (user_idx != -1) {
            int type_idx = -1;
            while (true) {
                if (!g_running) break;

                ui_wizard_clear();
                ui_wizard_print(0, 0, "TRANSACTION TYPE:", CP_HEADER);
                ui_wizard_print(1, 0, "[1] DEPOSIT  [2] WITHDRAWAL  [3] TRANSFER", CP_PRODUCER);
                if (!wizard_read_line("Type (1-3 or q): ", line, sizeof(line)) || is_quit(line)) break;
                
                int val = atoi(line);
                if (val >= 1 && val <= 3) { type_idx = val - 1; break; }
                ui_wizard_print(3, 0, "INVALID TYPE! Please enter 1-3.", CP_ERROR);
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
            }

            if (type_idx != -1) {
                const char* txn_type = TXN_TYPES[type_idx];
                
                double amt = -1;
                while (true) {
                    if (!g_running) break;

                    double current_balance = db_get_balance(conn, USERS[user_idx].id);
                    char bal_line[64];
                    snprintf(bal_line, 64, "Current Balance: $%.2f", current_balance);

                    ui_wizard_clear();
                    ui_wizard_print(0, 0, "ENTER AMOUNT:", CP_HEADER);
                    ui_wizard_print(1, 0, bal_line, CP_PRODUCER);
                    if (!wizard_read_line("Amount $: ", line, sizeof(line)) || is_quit(line)) break;
                    
                    char* endptr;
                    amt = strtod(line, &endptr);
                    if (*endptr == '\0' && amt > 0) break;
                    
                    amt = -1;
                    ui_wizard_print(3, 0, "INVALID AMOUNT! Must be a positive number.", CP_ERROR);
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                }

                if (amt > 0) {
                    int rec_id = 0;
                    char rec_name[32] = "";

                    if (strcmp(txn_type, "TRANSFER") == 0) {
                        while (true) {
                            if (!g_running) break;

                            ui_wizard_clear();
                            ui_wizard_print(0, 0, "SELECT RECIPIENT (1-5):", CP_HEADER);
                            for (int i = 0; i < 5; i++) {
                                if (i == user_idx) continue;
                                char rline[64];
                                snprintf(rline, 64, "[%d] %s", USERS[i].id, USERS[i].name);
                                ui_wizard_print(1, i * 15, rline, CP_PRODUCER);
                            }
                            if (!wizard_read_line("Recipient (1-5 or q): ", line, sizeof(line)) || is_quit(line)) { rec_id = -1; break; }
                            
                            int r_idx = atoi(line) - 1;
                            if (r_idx >= 0 && r_idx < 5 && r_idx != user_idx) {
                                rec_id = USERS[r_idx].id;
                                strncpy(rec_name, USERS[r_idx].name, 31);
                                break;
                            }
                            ui_wizard_print(3, 0, "INVALID RECIPIENT! Choice must be different from sender.", CP_ERROR);
                            std::this_thread::sleep_for(std::chrono::milliseconds(800));
                        }
                    }

                    if (rec_id != -1) {
                        Transaction txn;
                        pthread_mutex_lock(&g_txn_id_mutex);
                        txn.transaction_id = g_next_txn_id++;
                        pthread_mutex_unlock(&g_txn_id_mutex);
                        txn.user_id = USERS[user_idx].id;
                        txn.amount = amt;
                        txn.timestamp = time(nullptr);
                        txn.retry_count = 0;
                        txn.recipient_id = rec_id;
                        if (rec_id > 0) strncpy(txn.recipient_name, rec_name, 31);
                        strncpy(txn.transaction_type, txn_type, 11);
                        
                        db_insert_raw_transaction(conn, txn.transaction_id, txn.user_id, txn.amount, std::string(txn.transaction_type));
                        logger_log(ThreadType::PRODUCER, id, "Manual Transaction: " + std::string(USERS[user_idx].name) + " -> " + txn_type + " $" + std::to_string((int)amt));
                        
                        shm_buffer_produce(buf, txn);
                        ui_queue_push(txn.transaction_id, USERS[user_idx].name, txn.transaction_type, txn.amount);
                        
                        ui_wizard_clear();
                        ui_wizard_print(1, 0, "Transaction Submitted Successfully!", CP_SUCCESS);
                        std::this_thread::sleep_for(std::chrono::milliseconds(800));
                    }
                }
            }
        }
        ui_wizard_shutdown();
        nodelay(stdscr, TRUE);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  db_close_connection(conn);

  if (manual_only) {
    g_running = 0;
  }
  return nullptr;
}

// Blocks to read a line of input from the UI, updating an atomic flag to pause background logging.
bool wizard_read_line(const char *prompt, char *buf, int buf_size) {
  pthread_mutex_lock(&g_input_mutex);
  g_input_active = true;
  pthread_mutex_unlock(&g_input_mutex);
  bool ok = ui_wizard_get_string(buf, buf_size, prompt);
  pthread_mutex_lock(&g_input_mutex);
  g_input_active = false;
  pthread_mutex_unlock(&g_input_mutex);
  if (!ok || strlen(buf) == 0)
    return false;
  return true;
}

// Checks if a user-entered string is a termination command.
bool is_quit(const char *s) {
  return strcmp(s, "quit") == 0 || strcmp(s, "exit") == 0 ||
         strcmp(s, "q") == 0;
}

// Generates a random transaction and inserts it into the database; uses atomic increment for unique IDs.
Transaction make_transaction(sqlite3* conn, int thread_id) {
  Transaction txn;
  pthread_mutex_lock(&g_txn_id_mutex);
  txn.transaction_id = g_next_txn_id++;
  pthread_mutex_unlock(&g_txn_id_mutex);

  int sender_idx = rand() % NUM_USERS;
  txn.user_id = USERS[sender_idx].id;

  txn.amount = (double)((rand() % 19) + 1) * 50.0;
  int type_idx = rand() % 3;
  strncpy(txn.transaction_type, TXN_TYPES[type_idx], MAX_TYPE_LEN - 1);
  txn.timestamp = time(nullptr);
  txn.retry_count = 0;

  if (strcmp(txn.transaction_type, "TRANSFER") == 0) {
    int rec_idx;
    do {
      rec_idx = rand() % NUM_USERS;
    } while (rec_idx == sender_idx);

    txn.recipient_id = USERS[rec_idx].id;
    strncpy(txn.recipient_name, USERS[rec_idx].name, MAX_NAME_LEN - 1);
  }

  db_insert_raw_transaction(conn, txn.transaction_id, txn.user_id, txn.amount,
                            std::string(txn.transaction_type));

  std::string msg = "New transaction created  |  " +
                    std::string(get_user_name(txn.user_id)) + "  |  " +
                    txn.transaction_type + "  |  $" +
                    std::to_string((int)txn.amount);

  if (txn.recipient_id > 0)
    msg += "  |  To: " + std::string(txn.recipient_name);

  logger_log(ThreadType::PRODUCER, thread_id, msg);
  return txn;
}