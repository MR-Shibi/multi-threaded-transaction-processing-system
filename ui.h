#ifndef UI_H
#define UI_H

// ============================================================
//  ui.h
//  TERMINAL UI SYSTEM
//
//  Provides ANSI color codes, bordered panels, formatted
//  log lines, input prompts, and pipeline visualizations.
//
//  Design: industrial/utilitarian dark-terminal aesthetic.
//  Every thread type has its own color. Pipeline stages
//  are visually distinct. Input is guided and validated.
// ============================================================

#include <string>
#include <cstdio>

// ============================================================
//  ANSI ESCAPE CODES
//  These are standard terminal control sequences supported
//  by all modern terminals (Linux, WSL, macOS Terminal).
//  Format: \033[<code>m  where \033 is the ESC character.
// ============================================================

// ── Reset ────────────────────────────────────────────────────
#define ANSI_RESET       "\033[0m"

// ── Text styles ──────────────────────────────────────────────
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"
#define ANSI_ITALIC      "\033[3m"
#define ANSI_UNDERLINE   "\033[4m"
#define ANSI_BLINK       "\033[5m"
#define ANSI_REVERSE     "\033[7m"

// ── Foreground colors ────────────────────────────────────────
#define ANSI_BLACK       "\033[30m"
#define ANSI_RED         "\033[31m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_WHITE       "\033[37m"

// ── Bright foreground colors ─────────────────────────────────
#define ANSI_BR_BLACK    "\033[90m"
#define ANSI_BR_RED      "\033[91m"
#define ANSI_BR_GREEN    "\033[92m"
#define ANSI_BR_YELLOW   "\033[93m"
#define ANSI_BR_BLUE     "\033[94m"
#define ANSI_BR_MAGENTA  "\033[95m"
#define ANSI_BR_CYAN     "\033[96m"
#define ANSI_BR_WHITE    "\033[97m"

// ── Background colors ────────────────────────────────────────
#define ANSI_BG_BLACK    "\033[40m"
#define ANSI_BG_RED      "\033[41m"
#define ANSI_BG_GREEN    "\033[42m"
#define ANSI_BG_YELLOW   "\033[43m"
#define ANSI_BG_BLUE     "\033[44m"
#define ANSI_BG_MAGENTA  "\033[45m"
#define ANSI_BG_CYAN     "\033[46m"
#define ANSI_BG_WHITE    "\033[47m"

// ── Thread-type color scheme ─────────────────────────────────
// Each thread type has a unique color identity — consistent
// throughout all log output so you can instantly identify
// which component is speaking at a glance.
#define UI_COLOR_PRODUCER   ANSI_BR_CYAN
#define UI_COLOR_VALIDATOR  ANSI_BR_YELLOW
#define UI_COLOR_UPDATER    ANSI_BR_GREEN
#define UI_COLOR_MONITOR    ANSI_BR_MAGENTA
#define UI_COLOR_SYSTEM     ANSI_BR_WHITE
#define UI_COLOR_ERROR      ANSI_BR_RED
#define UI_COLOR_SUCCESS    ANSI_BR_GREEN
#define UI_COLOR_WARNING    ANSI_BR_YELLOW
#define UI_COLOR_DIM        ANSI_BR_BLACK
#define UI_COLOR_HEADER     ANSI_BR_WHITE

// ── Box-drawing characters (UTF-8) ───────────────────────────
// Used to draw bordered panels.
#define BOX_TL   "╔"
#define BOX_TR   "╗"
#define BOX_BL   "╚"
#define BOX_BR   "╝"
#define BOX_H    "═"
#define BOX_V    "║"
#define BOX_ML   "╠"
#define BOX_MR   "╣"
#define BOX_MH   "═"
#define BOX_TM   "╦"
#define BOX_BM   "╩"

// Thin box variants (for nested panels)
#define THIN_TL  "┌"
#define THIN_TR  "┐"
#define THIN_BL  "└"
#define THIN_BR  "┘"
#define THIN_H   "─"
#define THIN_V   "│"
#define THIN_ML  "├"
#define THIN_MR  "┤"

// Arrow symbols for pipeline flow
#define ARROW_DOWN  "  ▼  "
#define ARROW_RIGHT " ──► "
#define BULLET      " ●  "
#define CHECK       " ✓  "
#define CROSS       " ✗  "
#define WARN        " ⚠  "
#define INFO        " ℹ  "
#define PIPE_SYM    " ║  "

// ── Panel width ───────────────────────────────────────────────
static const int UI_WIDTH = 64;  // total terminal width of panels

// ============================================================
//  FUNCTION DECLARATIONS
// ============================================================

// ── Startup / shutdown visuals ───────────────────────────────
void ui_print_banner(bool auto_mode, bool manual_mode);
void ui_print_shutdown_banner();
void ui_print_final_report(int generated, int done, int rejected,
                           int pending, int processing, int committed);

// ── Pipeline flow animation ───────────────────────────────────
// Called between major state transitions in simulation mode.
// Prints a colored arrow showing data moving between stages.
void ui_animate_transition(const char* from_stage,
                           const char* to_stage,
                           const char* detail);

// ── Manual input UI ──────────────────────────────────────────
void ui_print_input_panel();          // draw the input help panel
void ui_print_input_prompt();         // print the styled "txn>" prompt
void ui_print_input_success(int txn_id, int user_id,
                             double amount, const char* type);
void ui_print_input_error(const char* field, const char* reason);
void ui_print_input_hint(const char* hint);

// ── Log line formatting ──────────────────────────────────────
// Returns a fully formatted, colored log line ready for printf.
std::string ui_format_log(const char* thread_type,
                           int         thread_num,
                           const char* message,
                           const char* timestamp);

// ── Section headers ──────────────────────────────────────────
void ui_print_section(const char* title);

// ── Monitor snapshot panel ────────────────────────────────────
void ui_print_monitor_snapshot(int snapshot_num,
                                int buf_count, int buf_total,
                                int done, int rejected,
                                int pending, int processing,
                                int committed, double tps);

// ── Utility ──────────────────────────────────────────────────
// Repeat a string n times into a std::string
std::string ui_repeat(const char* s, int n);

// Build a fixed-width string (truncate or pad with spaces)
std::string ui_fixed(const std::string& s, int width);

// Print a full-width horizontal rule
void ui_rule(const char* color = ANSI_BR_BLACK);

// Clear current terminal line (for overwriting prompt)
void ui_clear_line();

#endif // UI_H