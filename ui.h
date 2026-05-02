#ifndef UI_H
#define UI_H

// ============================================================
//  ui.h — Terminal UI System
// ============================================================

#include <string>
#include <cstdio>

// ── ANSI codes ───────────────────────────────────────────────
#define ANSI_RESET       "\033[0m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"
#define ANSI_UNDERLINE   "\033[4m"

#define ANSI_BLACK       "\033[30m"
#define ANSI_RED         "\033[31m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_WHITE       "\033[37m"

#define ANSI_BR_BLACK    "\033[90m"
#define ANSI_BR_RED      "\033[91m"
#define ANSI_BR_GREEN    "\033[92m"
#define ANSI_BR_YELLOW   "\033[93m"
#define ANSI_BR_BLUE     "\033[94m"
#define ANSI_BR_MAGENTA  "\033[95m"
#define ANSI_BR_CYAN     "\033[96m"
#define ANSI_BR_WHITE    "\033[97m"

#define ANSI_BG_BLACK    "\033[40m"
#define ANSI_BG_RED      "\033[41m"
#define ANSI_BG_GREEN    "\033[42m"
#define ANSI_BG_YELLOW   "\033[43m"
#define ANSI_BG_BLUE     "\033[44m"
#define ANSI_BG_MAGENTA  "\033[45m"
#define ANSI_BG_CYAN     "\033[46m"
#define ANSI_BG_WHITE    "\033[47m"

// ── Thread color identity ────────────────────────────────────
#define UI_COLOR_PRODUCER   ANSI_BR_CYAN
#define UI_COLOR_VALIDATOR  ANSI_BR_YELLOW
#define UI_COLOR_UPDATER    ANSI_BR_GREEN
#define UI_COLOR_MONITOR    ANSI_BR_MAGENTA
#define UI_COLOR_SYSTEM     ANSI_BR_WHITE
#define UI_COLOR_ERROR      ANSI_BR_RED
#define UI_COLOR_SUCCESS    ANSI_BR_GREEN
#define UI_COLOR_WARNING    ANSI_BR_YELLOW
#define UI_COLOR_DIM        ANSI_BR_BLACK

// ── Box drawing (UTF-8) ──────────────────────────────────────
#define BOX_TL   "╔"
#define BOX_TR   "╗"
#define BOX_BL   "╚"
#define BOX_BR   "╝"
#define BOX_H    "═"
#define BOX_V    "║"
#define BOX_ML   "╠"
#define BOX_MR   "╣"
#define BOX_MH   "═"

#define THIN_TL  "┌"
#define THIN_TR  "┐"
#define THIN_BL  "└"
#define THIN_BR  "┘"
#define THIN_H   "─"
#define THIN_V   "│"
#define THIN_ML  "├"
#define THIN_MR  "┤"

// ── Symbols ──────────────────────────────────────────────────
#define CHECK    " OK "
#define CROSS    " !! "
#define WARN     " >> "

// ── Panel width ───────────────────────────────────────────────
static const int UI_WIDTH = 64;

// ── Configurable auto-mode transition delay ──────────────────
// Change this value to slow down or speed up auto mode:
//   500000  = 0.5 seconds (fast demo)
//   1000000 = 1.0 second  (balanced — default)
//   2000000 = 2.0 seconds (slow, easy to follow)
static const int AUTO_TRANSITION_DELAY_US = 2000000;

// ============================================================
//  FUNCTION DECLARATIONS
// ============================================================

// ── Startup / Shutdown ───────────────────────────────────────
void ui_print_banner(bool auto_mode, bool manual_mode);
void ui_print_shutdown_banner();
void ui_print_final_report(int generated, int done, int rejected,
                           int pending, int processing, int committed);

// ── Step-by-step transaction wizard ─────────────────────────
void ui_wizard_show_users();
void ui_wizard_show_types(int user_id, const char* user_name);

// NEW: shown when user picks TRANSFER — asks for recipient
void ui_wizard_show_transfer_recipient(int sender_id,
                                        const char* sender_name);

void ui_wizard_show_amount(int user_id, const char* user_name,
                            const char* txn_type,
                            double current_balance,
                            int recipient_id,
                            const char* recipient_name);

void ui_wizard_warn_no_session(const char* user_name);
void ui_wizard_show_confirm(int user_id,    const char* user_name,
                             const char* txn_type, double amount,
                             int recipient_id, const char* recipient_name);
void ui_wizard_show_queued(int txn_id, int user_id,
                            const char* user_name,
                            const char* txn_type, double amount,
                            int recipient_id, const char* recipient_name);
void ui_wizard_show_cancelled();
void ui_wizard_prompt(const char* step_label);
void ui_wizard_error(const char* msg);
void ui_wizard_ask_another();

// ── Legacy functions (used by other files) ───────────────────
void ui_print_input_panel();
void ui_print_input_prompt();
void ui_print_input_error(const char* field, const char* reason);
void ui_print_input_success(int txn_id, int user_id,
                             double amount, const char* type);
void ui_print_input_hint(const char* hint);

// ── Pipeline animation ────────────────────────────────────────
void ui_animate_transition(const char* from_stage,
                           const char* to_stage,
                           const char* detail);

// ── Log line formatter ────────────────────────────────────────
std::string ui_format_log(const char* thread_type,
                           int         thread_num,
                           const char* message,
                           const char* timestamp);

// ── Section divider ──────────────────────────────────────────
void ui_print_section(const char* title);

// ── Monitor snapshot panel ────────────────────────────────────
void ui_print_monitor_snapshot(int snapshot_num,
                                int buf_count, int buf_total,
                                int done, int rejected,
                                int pending, int processing,
                                int committed, double tps);

// ── Utilities ─────────────────────────────────────────────────
std::string ui_repeat(const char* s, int n);
std::string ui_fixed(const std::string& s, int width);
void ui_rule(const char* color = ANSI_BR_BLACK);
void ui_clear_line();

#endif // UI_H