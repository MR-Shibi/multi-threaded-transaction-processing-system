#ifndef UI_H
#define UI_H

#include <ncurses.h>
#include <mutex>

#define CP_HEADER    1
#define CP_PRODUCER  2
#define CP_VALIDATOR 3
#define CP_UPDATER   4
#define CP_MONITOR   5
#define CP_SYSTEM    6
#define CP_ERROR     7
#define CP_SUCCESS   8
#define CP_BORDER    9
#define CP_FOOTER   10
#define CP_WIZARD   11

// Initializes the ncurses-based user interface; requires global UI mutex for safe access.
void ui_init();
// Shuts down ncurses and cleans up UI windows; protected by the UI mutex.
void ui_shutdown();

// Updates the top header with the current system mode and timestamp.
void ui_update_header(bool auto_mode, bool manual_mode);

// Updates the status display for a specific thread, using a mutex to prevent screen corruption.
void ui_set_thread_status(const char* type, int num, const char* status);

// Adds a transaction to the visual queue representation in the UI.
void ui_queue_push(int txn_id, const char* user_name,
                   const char* type, double amount);
// Removes an item from the visual queue representation.
void ui_queue_pop();

// Adds a new log entry to the event log window; thread-safe via UI mutex.
void ui_add_log(const char* thread_type, int thread_num,
                const char* message, const char* timestamp);

// Updates the system monitor window with the latest performance statistics.
void ui_update_monitor(int snapshot_num,
                        int buf_count, int buf_total,
                        int done, int rejected,
                        int pending, int processing,
                        int committed, double tps,
                        int deposits, int withdrawals, int transfers);

// Pushes a completed transaction to the visual history log.
void ui_history_push(int txn_id, const char* type,
                     double amount, bool saved);

// Clears the manual transaction wizard window.
void ui_wizard_clear();
// Removes the wizard window from the UI.
void ui_wizard_shutdown();
// Prints text at a specific position within the wizard window.
void ui_wizard_print(int row, int col, const char* text, int color_pair = 0);
// Captures user input within the wizard window; blocks caller thread.
bool ui_wizard_get_string(char* buf, int max_len, const char* prompt);

// Prints the final system report to stdout after the UI is shut down.
void ui_show_final_report(int generated, int done, int rejected, int pending,
                           int processing, int forwarded, int failed,
                           int committed);

extern std::mutex g_ui_mutex;

#endif // UI_H