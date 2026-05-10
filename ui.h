#ifndef UI_H
#define UI_H

#include <ncurses.h>
#include <mutex>

// ── Color pairs ──────────────────────────────────────────────
#define CP_HEADER    1  // White on Teal   — top bar
#define CP_PRODUCER  2  // Cyan on Black
#define CP_VALIDATOR 3  // Yellow on Black
#define CP_UPDATER   4  // Green on Black
#define CP_MONITOR   5  // Magenta on Black
#define CP_SYSTEM    6  // White on Black
#define CP_ERROR     7  // Red on Black
#define CP_SUCCESS   8  // Bright Green on Black
#define CP_BORDER    9  // Blue on Black
#define CP_FOOTER   10  // Black on White  — bottom bar

// ── Lifecycle ─────────────────────────────────────────────────
void ui_init();
void ui_shutdown();

// ── Header bar (full-width, teal, with live clock) ────────────
void ui_update_header(bool auto_mode, bool manual_mode);

// ── Pipeline Status panel (top-left) ─────────────────────────
// Updates one thread's status line. Redraws the whole panel.
// type = "PRODUCER" | "VALIDATOR" | "UPDATER"
void ui_set_thread_status(const char* type, int num, const char* status);

// ── Buffer Queue panel (top-right) ───────────────────────────
// Call from producer when a transaction is pushed into the buffer.
void ui_queue_push(int txn_id, const char* user_name,
                   const char* type, double amount);
// Call from validator when a transaction is removed from the buffer.
void ui_queue_pop();

// ── Event Log panel (right-middle, scrolling) ─────────────────
void ui_add_log(const char* thread_type, int thread_num,
                const char* message, const char* timestamp);

// ── Metrics panel (left-middle) ──────────────────────────────
void ui_update_monitor(int snapshot_num,
                        int buf_count, int buf_total,
                        int done, int rejected,
                        int pending, int processing,
                        int committed, double tps,
                        int deposits, int withdrawals, int transfers);

// ── Transaction History table (bottom, scrolling) ─────────────
// Call from updater after a commit.
void ui_history_push(int txn_id, const char* type,
                     double amount, bool saved);

// ── Wizard overlay (manual input) ────────────────────────────
void ui_wizard_clear();
void ui_wizard_print(int row, int col, const char* text, int color_pair = 0);
void ui_wizard_get_string(char* buf, int max_len, const char* prompt);

// ── Final report (after endwin) ──────────────────────────────
void ui_show_final_report(int generated, int done, int rejected,
                           int pending, int processing, int committed);

// ── Thread safety ─────────────────────────────────────────────
extern std::mutex g_ui_mutex;

#endif // UI_H