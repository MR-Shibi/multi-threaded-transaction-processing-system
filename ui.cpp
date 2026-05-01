// ============================================================
//  ui.cpp — Terminal UI System (safe rewrite)
//
//  All padding calculations use only VISIBLE character counts.
//  No ANSI codes are mixed into width arithmetic.
// ============================================================

#include "ui.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

// ── Width of the terminal panels (visible chars) ─────────────
static const int W = UI_WIDTH;   // 64

// ============================================================
//  UTILITY
// ============================================================

std::string ui_repeat(const char* s, int n) {
    if (n <= 0) return "";
    std::string r;
    for (int i = 0; i < n; i++) r += s;
    return r;
}

std::string ui_fixed(const std::string& s, int width) {
    if (width <= 0) return "";
    if ((int)s.size() >= width) return s.substr(0, width);
    return s + std::string(width - s.size(), ' ');
}

void ui_rule(const char* color) {
    printf("%s%s%s\n", color, ui_repeat("─", W).c_str(), ANSI_RESET);
    fflush(stdout);
}

void ui_clear_line() {
    printf("\r\033[2K");
    fflush(stdout);
}

// ============================================================
//  INTERNAL HELPERS
// ============================================================

// Print one row inside a thick box.
// |  <content padded to W-4>  |
// content_visible_len: visible length of content string
// (needed because content may contain ANSI codes)
static void box_row(const std::string& content,
                    int  content_visible_len,
                    const char* border_color = ANSI_BR_BLACK) {
    int pad = (W - 4) - content_visible_len;
    if (pad < 0) pad = 0;
    printf("%s%s%s  %s  %s%s%s\n",
           ANSI_BOLD, border_color, BOX_V,
           content.c_str(),
           std::string(pad, ' ').c_str(),
           ANSI_BOLD, border_color, BOX_V,
           ANSI_RESET);
}

static void box_blank(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color, BOX_V,
           std::string(W - 2, ' ').c_str(),
           BOX_V, ANSI_RESET);
}

static void box_top_titled(const char* title,
                            const char* border_color = ANSI_BR_BLACK) {
    int inner  = W - 2;
    int t_vis  = (int)strlen(title);
    int left   = (inner - t_vis) / 2;
    int right  = inner - t_vis - left;
    if (left  < 0) left  = 0;
    if (right < 0) right = 0;

    printf("%s%s%s%s%s%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_TL, ui_repeat(BOX_H, left).c_str(),
           ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE,
           title,
           ANSI_RESET, ANSI_BOLD, border_color,
           ui_repeat(BOX_H, right).c_str(), BOX_TR,
           ANSI_RESET);
}

static void box_top(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_TL, ui_repeat(BOX_H, W - 2).c_str(), BOX_TR,
           ANSI_RESET);
}

static void box_divider(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_ML, ui_repeat(BOX_H, W - 2).c_str(), BOX_MR,
           ANSI_RESET);
}

static void box_bottom(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_BL, ui_repeat(BOX_H, W - 2).c_str(), BOX_BR,
           ANSI_RESET);
}

// ============================================================
//  ui_print_banner()
// ============================================================
void ui_print_banner(bool auto_mode, bool manual_mode) {
    printf("\n");
    box_top_titled(" Multi-Threaded Transaction Processing System ",
                   ANSI_BR_WHITE);

    // Subtitle row (plain text, no ANSI in visible length)
    {
        const char* sub = "OS Project  -  Full System Integration";
        int vis = (int)strlen(sub);
        std::string content =
            std::string(ANSI_DIM) + ANSI_WHITE + sub + ANSI_RESET;
        box_row(content, vis, ANSI_BR_WHITE);
    }

    box_divider(ANSI_BR_WHITE);
    box_blank(ANSI_BR_WHITE);

    // Pipeline rows
    auto pipe_row = [&](const char* label, const char* label_color,
                         const char* detail) {
        // label visible = strlen(label), padded to 18
        // detail visible = strlen(detail)
        int lpad  = 18 - (int)strlen(label);
        if (lpad < 0) lpad = 0;
        int vis = 18 + 2 + (int)strlen(detail);

        std::string content =
            std::string(ANSI_BOLD) + label_color + label + ANSI_RESET
            + std::string(lpad, ' ')
            + "  "
            + ANSI_DIM + ANSI_WHITE + detail + ANSI_RESET;
        box_row(content, vis, ANSI_BR_WHITE);
    };

    auto arrow_row = [&](const char* text) {
        int vis = 9 + (int)strlen(text);   // "         " prefix
        std::string content =
            std::string(ANSI_BR_BLACK)
            + "         " + text
            + ANSI_RESET;
        box_row(content, vis, ANSI_BR_WHITE);
    };

    if (auto_mode)
        pipe_row("PRODUCER [AUTO]",   UI_COLOR_PRODUCER,
                 "SLOW · FAST · BURST modes");
    if (manual_mode)
        pipe_row("PRODUCER [MANUAL]", UI_COLOR_PRODUCER,
                 "Keyboard input at txn> prompt");

    arrow_row("v  [Shared Memory]  /dev/shm/txn_shared_buffer");
    arrow_row("   Circular buffer  8 slots  sem + mutex sync");

    pipe_row("VALIDATOR",          UI_COLOR_VALIDATOR,
             "Session check  Balance check  SQL build");

    arrow_row("v  [Named Pipe FIFO]  /tmp/txn_query_pipe");
    arrow_row("   512-byte atomic SQL messages");

    pipe_row("DB UPDATER",         UI_COLOR_UPDATER,
             "BEGIN  INSERT + UPDATE  COMMIT");

    arrow_row("v  [SQLite3 WAL]  transactions.db");
    arrow_row("   4 tables  mutex protected  persistent");

    box_blank(ANSI_BR_WHITE);
    box_divider(ANSI_BR_WHITE);

    pipe_row("LOGGER",  UI_COLOR_SYSTEM,
             "Sole stdout owner  async queue");
    pipe_row("MONITOR", UI_COLOR_MONITOR,
             "Read-only  1s interval  live panels");

    box_blank(ANSI_BR_WHITE);
    box_divider(ANSI_BR_WHITE);

    {
        const char* ctrl = "Ctrl+C  =>  graceful shutdown";
        int vis = (int)strlen(ctrl);
        std::string content =
            std::string(ANSI_BR_WHITE) + ctrl + ANSI_RESET;
        box_row(content, vis, ANSI_BR_WHITE);
    }
    if (manual_mode) {
        const char* fmt = "Input format:  user_id  amount  type";
        int vis = (int)strlen(fmt);
        std::string content =
            std::string(ANSI_WHITE) + fmt + ANSI_RESET;
        box_row(content, vis, ANSI_BR_WHITE);

        const char* eg = "Examples:  3 250 WITHDRAWAL   2 500 DEPOSIT";
        vis = (int)strlen(eg);
        content = std::string(ANSI_DIM) + ANSI_WHITE + eg + ANSI_RESET;
        box_row(content, vis, ANSI_BR_WHITE);
    }

    box_blank(ANSI_BR_WHITE);
    box_bottom(ANSI_BR_WHITE);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  ui_print_input_panel()
// ============================================================
void ui_print_input_panel() {
    const char* bc = UI_COLOR_PRODUCER;
    int inner = W - 2;

    printf("\n%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_TL, ui_repeat(THIN_H, inner).c_str(), THIN_TR,
           ANSI_RESET);

    auto row = [&](const char* label, const char* value,
                   const char* vc = ANSI_BR_WHITE) {
        // visible = strlen(label) + strlen(value) + spaces
        int llen = (int)strlen(label);
        int vlen = (int)strlen(value);
        int pad  = (W - 4) - llen - vlen;
        if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, label, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, value, ANSI_RESET,
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    };

    row("  FORMAT  ", "user_id  amount  type",       ANSI_BR_CYAN);
    row("  EXAMPLE ", "3  250  WITHDRAWAL",           UI_COLOR_PRODUCER);
    row("  EXAMPLE ", "2  500  DEPOSIT",              UI_COLOR_PRODUCER);
    row("  EXAMPLE ", "1  100  TRANSFER",             UI_COLOR_PRODUCER);

    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_ML, ui_repeat(THIN_H, inner).c_str(), THIN_MR,
           ANSI_RESET);

    row("  USERS   ", "1=Alice  2=Bob  3=Charlie  4=Diana  5=Eve");
    row("  NOTE    ", "User 5 has NO session -> REJECTED", UI_COLOR_WARNING);
    row("  STOP    ", "Type  quit  to stop manual input",  ANSI_DIM);

    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_BL, ui_repeat(THIN_H, inner).c_str(), THIN_BR,
           ANSI_RESET);

    printf("\n");
    fflush(stdout);
}

// ============================================================
//  ui_print_input_prompt()
// ============================================================
void ui_print_input_prompt() {
    printf("%s%s txn %s%s%s >  %s",
           ANSI_BOLD, ANSI_BG_CYAN,
           ANSI_RESET, ANSI_BOLD, UI_COLOR_PRODUCER,
           ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_print_input_error()
// ============================================================
void ui_print_input_error(const char* field, const char* reason) {
    printf("\n  %s%s ERROR %s  %s%s%s: %s%s\n\n",
           ANSI_BOLD, ANSI_BG_RED,
           ANSI_RESET,
           ANSI_BR_RED, ANSI_BOLD, field,
           ANSI_RESET, ANSI_BR_WHITE, reason,
           ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_print_input_success()
// ============================================================
void ui_print_input_success(int txn_id, int user_id,
                             double amount, const char* type) {
    printf("\n  %s%s QUEUED %s  TXN #%d  User:%d  $%.0f  %s%s\n\n",
           ANSI_BOLD, ANSI_BG_GREEN,
           ANSI_RESET,
           txn_id, user_id, amount, type,
           ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_print_input_hint()
// ============================================================
void ui_print_input_hint(const char* hint) {
    printf("  %s%s%s  %s%s\n",
           ANSI_BOLD, ANSI_BR_YELLOW, WARN,
           hint, ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_animate_transition()
// ============================================================
void ui_animate_transition(const char* from_stage,
                           const char* to_stage,
                           const char* detail) {
    const char* frames[] = { ".", "─", "─", ">" };
    for (int i = 0; i < 4; i++) {
        printf("\r  %s%s%-14s%s  %s%s%s  %s%-14s%s  %s%s%s",
               ANSI_BOLD, UI_COLOR_PRODUCER, from_stage, ANSI_RESET,
               ANSI_BR_BLACK, frames[i], ANSI_RESET,
               ANSI_BOLD, UI_COLOR_VALIDATOR, to_stage, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, detail, ANSI_RESET);
        fflush(stdout);
        usleep(80000);
    }
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  ui_format_log()
// ============================================================
std::string ui_format_log(const char* thread_type,
                           int         thread_num,
                           const char* message,
                           const char* timestamp) {
    const char* color  = ANSI_BR_WHITE;
    const char* symbol = "*";

    if      (strcmp(thread_type, "PRODUCER")  == 0) { color = UI_COLOR_PRODUCER;  symbol = ">"; }
    else if (strcmp(thread_type, "VALIDATOR") == 0) { color = UI_COLOR_VALIDATOR; symbol = "#"; }
    else if (strcmp(thread_type, "UPDATER")   == 0) { color = UI_COLOR_UPDATER;   symbol = "+"; }
    else if (strcmp(thread_type, "MONITOR")   == 0) { color = UI_COLOR_MONITOR;   symbol = "o"; }
    else                                             { color = UI_COLOR_SYSTEM;    symbol = "."; }

    char label[24];
    if (thread_num > 0)
        snprintf(label, sizeof(label), "%s-%d", thread_type, thread_num);
    else
        snprintf(label, sizeof(label), "%s", thread_type);

    // Choose message color based on content
    std::string msg_str(message);
    const char* mc = ANSI_WHITE;
    if      (msg_str.find("REJECTED") != std::string::npos) mc = ANSI_BR_RED;
    else if (msg_str.find("VALID")     != std::string::npos ||
             msg_str.find("COMMITTED") != std::string::npos) mc = ANSI_BR_GREEN;
    else if (msg_str.find("WARNING")   != std::string::npos ||
             msg_str.find("FAILED")    != std::string::npos) mc = ANSI_BR_YELLOW;

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "%s%s[%s]%s %s%s%s%s %s%-14s%s %s%s%s",
        ANSI_DIM, ANSI_WHITE, timestamp, ANSI_RESET,
        ANSI_BOLD, color, symbol, ANSI_RESET,
        ANSI_BOLD, color, label, ANSI_RESET,
        mc, message, ANSI_RESET);

    return std::string(buf);
}

// ============================================================
//  ui_print_section()
// ============================================================
void ui_print_section(const char* title) {
    int t_len = (int)strlen(title);
    int side  = (W - t_len - 4) / 2;
    if (side < 0) side = 0;
    printf("\n%s%s%s %s%s%s%s %s%s%s\n\n",
           ANSI_BOLD, ANSI_BR_BLACK,
           ui_repeat("─", side).c_str(),
           ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE,
           title,
           ANSI_RESET, ANSI_BOLD, ANSI_BR_BLACK,
           ui_repeat("─", side).c_str(),
           ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_print_monitor_snapshot()
// ============================================================
void ui_print_monitor_snapshot(int snapshot_num,
                                int buf_count, int buf_total,
                                int done,      int rejected,
                                int pending,   int processing,
                                int committed, double tps) {
    const char* bc    = ANSI_BR_MAGENTA;
    int inner = W - 2;

    printf("\n");

    // Titled top border
    char title[32];
    snprintf(title, sizeof(title), " Snapshot #%d ", snapshot_num);
    int t_vis = (int)strlen(title);
    int left  = (inner - t_vis) / 2;
    int right = inner - t_vis - left;
    if (left  < 0) left  = 0;
    if (right < 0) right = 0;

    printf("%s%s%s%s%s%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_TL, ui_repeat(THIN_H, left).c_str(),
           ANSI_RESET, ANSI_BOLD, UI_COLOR_MONITOR,
           title,
           ANSI_RESET, ANSI_BOLD, bc,
           ui_repeat(THIN_H, right).c_str(), THIN_TR,
           ANSI_RESET);

    // Buffer bar row
    {
        // Build bar string and track visible length separately
        std::string bar_vis = "[";       // visible portion
        std::string bar_col = "[";       // with colors
        for (int i = 0; i < buf_total; i++) {
            if (i < buf_count) {
                bar_col += std::string(ANSI_BR_CYAN) + "#" + ANSI_RESET;
                bar_vis += "#";
            } else {
                bar_col += std::string(ANSI_BR_BLACK) + "." + ANSI_RESET;
                bar_vis += ".";
            }
        }
        bar_col += "]";
        bar_vis += "]";

        char suffix[32];
        snprintf(suffix, sizeof(suffix), "  %d/%d slots",
                 buf_count, buf_total);

        int vis = 2 + (int)strlen("Buffer 1 (SHM)  ")
                    + (int)bar_vis.size()
                    + (int)strlen(suffix);
        int pad = (W - 4) - vis;
        if (pad < 0) pad = 0;

        printf("%s%s%s  %sBuffer 1 (SHM)  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               bar_col.c_str(),
               ANSI_WHITE, suffix, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    }

    // Divider
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_ML, ui_repeat(THIN_H, inner).c_str(), THIN_MR,
           ANSI_RESET);

    // Stats rows
    auto stat = [&](const char* label, int val, const char* vc) {
        char num[16];
        snprintf(num, sizeof(num), "%d", val);
        int vis = (int)strlen(label) + (int)strlen(num);
        int pad = (W - 4) - vis;
        if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_WHITE, label, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, num, ANSI_RESET,
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    };

    stat("raw  DONE (validated)    ", done,       ANSI_BR_GREEN);
    stat("raw  REJECTED            ", rejected,   ANSI_BR_RED);
    stat("raw  PENDING             ", pending,
         pending > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    stat("raw  PROCESSING          ", processing,
         processing > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    stat("txn  COMMITTED (final)   ", committed,  ANSI_BR_GREEN);

    // Divider
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_ML, ui_repeat(THIN_H, inner).c_str(), THIN_MR,
           ANSI_RESET);

    // Throughput row
    {
        char tps_str[16];
        snprintf(tps_str, sizeof(tps_str), "%.1f txn/sec", tps);
        int vis = (int)strlen("Throughput               ")
                + (int)strlen(tps_str);
        int pad = (W - 4) - vis;
        if (pad < 0) pad = 0;
        printf("%s%s%s  %sThroughput               %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_WHITE,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, ANSI_BR_CYAN, tps_str, ANSI_RESET,
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    }

    // Warning row
    if (pending + processing > 0) {
        char warn[64];
        snprintf(warn, sizeof(warn),
                 "WARNING: %d stuck in PENDING/PROCESSING",
                 pending + processing);
        int vis = (int)strlen(warn);
        int pad = (W - 4) - vis;
        if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_BOLD, ANSI_BR_YELLOW, warn, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    }

    // Bottom
    printf("%s%s%s%s%s%s\n\n",
           ANSI_BOLD, bc,
           THIN_BL, ui_repeat(THIN_H, inner).c_str(), THIN_BR,
           ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_print_final_report()
// ============================================================
void ui_print_final_report(int generated, int done, int rejected,
                           int pending,   int processing, int committed) {
    const char* bc   = ANSI_BR_WHITE;
    bool audit_ok    = (generated == (done + rejected + pending + processing));
    bool pipeline_ok = (done == committed);

    printf("\n");
    box_top_titled(" FINAL SYSTEM REPORT ", bc);

    box_blank(bc);

    auto row = [&](const char* label, int val, const char* vc) {
        char num[16];
        snprintf(num, sizeof(num), "%d", val);
        int vis = (int)strlen(label) + (int)strlen(num);
        int pad = (W - 4) - vis;
        if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_WHITE, label,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, num, ANSI_RESET,
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    };

    row("Transactions generated         ", generated,  ANSI_BR_CYAN);
    box_blank(bc);
    box_divider(bc);
    box_blank(bc);
    row("raw_transactions  DONE         ", done,       ANSI_BR_GREEN);
    row("raw_transactions  REJECTED     ", rejected,   ANSI_BR_RED);
    row("raw_transactions  PENDING      ", pending,
        pending > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    row("raw_transactions  PROCESSING   ", processing,
        processing > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    box_blank(bc);
    row("transactions  COMMITTED (PAID) ", committed,  ANSI_BR_GREEN);
    box_blank(bc);
    box_divider(bc);
    box_blank(bc);

    // Check rows
    auto check_row = [&](const char* label, bool ok) {
        const char* vc   = ok ? ANSI_BR_GREEN : ANSI_BR_RED;
        const char* word = ok ? "PASS" : "FAIL";
        const char* sym  = ok ? CHECK  : CROSS;
        int vis = 3 + (int)strlen(label) + 4;
        int pad = (W - 4) - vis;
        if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_BOLD, vc, sym, ANSI_RESET,
               ANSI_WHITE, label,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, word, ANSI_RESET,
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    };

    check_row("Audit check    (generated == raw total)", audit_ok);
    check_row("Pipeline check (DONE == committed)     ", pipeline_ok);
    box_blank(bc);
    box_divider(bc);

    // DB hint
    {
        const char* h1 = "  sqlite3 transactions.db";
        int vis = (int)strlen(h1);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, h1,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);

        const char* h2 = "  SELECT * FROM transactions;";
        vis = (int)strlen(h2); pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, h2,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);

        const char* h3 = "  SELECT user_id, name, balance FROM users;";
        vis = (int)strlen(h3); pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, h3,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }

    box_blank(bc);
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  ui_print_shutdown_banner()
// ============================================================
void ui_print_shutdown_banner() {
    printf("\n%s%s  WARNING  Shutdown signal — stopping gracefully...%s\n\n",
           ANSI_BOLD, ANSI_BR_YELLOW, ANSI_RESET);
    fflush(stdout);
}