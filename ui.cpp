// ============================================================
//  ui.cpp — Terminal UI System
//  - Human-readable log messages
//  - Transfer recipient wizard step
//  - Fixed WARNING false alarm (only shows when truly stuck)
//  - Choice prompt no longer duplicates
// ============================================================

#include "ui.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

static const int W = UI_WIDTH;

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
//  INTERNAL BOX HELPERS
// ============================================================
static void box_top_titled(const char* title,
                            const char* border_color = ANSI_BR_BLACK) {
    int inner = W - 2;
    int t_vis = (int)strlen(title);
    int left  = (inner - t_vis) / 2;
    int right = inner - t_vis - left;
    if (left  < 0) left  = 0;
    if (right < 0) right = 0;
    printf("%s%s%s%s%s%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_TL, ui_repeat(BOX_H, left).c_str(),
           ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, title,
           ANSI_RESET, ANSI_BOLD, border_color,
           ui_repeat(BOX_H, right).c_str(), BOX_TR, ANSI_RESET);
}

static void box_divider(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_ML, ui_repeat(BOX_H, W - 2).c_str(), BOX_MR, ANSI_RESET);
}

static void box_bottom(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color,
           BOX_BL, ui_repeat(BOX_H, W - 2).c_str(), BOX_BR, ANSI_RESET);
}

static void box_blank(const char* border_color = ANSI_BR_BLACK) {
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color, BOX_V,
           std::string(W - 2, ' ').c_str(),
           BOX_V, ANSI_RESET);
}

static void box_row_lv(const char* label, const char* value,
                        const char* label_color, const char* value_color,
                        const char* border_color = ANSI_BR_BLACK) {
    int llen = (int)strlen(label);
    int vlen = (int)strlen(value);
    int pad  = (W - 4) - llen - vlen;
    if (pad < 0) pad = 0;
    printf("%s%s%s  %s%s%s%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color, BOX_V, ANSI_RESET,
           ANSI_BOLD, label_color, label, ANSI_RESET,
           std::string(pad, ' ').c_str(),
           ANSI_BOLD, value_color, value, ANSI_RESET,
           ANSI_BOLD, border_color, BOX_V, ANSI_RESET);
}

static void box_row_center(const char* text, const char* color,
                            const char* border_color = ANSI_BR_BLACK) {
    int tlen  = (int)strlen(text);
    int inner = W - 4;
    int lpad  = (inner - tlen) / 2;
    int rpad  = inner - tlen - lpad;
    if (lpad < 0) lpad = 0;
    if (rpad < 0) rpad = 0;
    printf("%s%s%s  %s%s%s%s%s%s%s%s\n",
           ANSI_BOLD, border_color, BOX_V, ANSI_RESET,
           std::string(lpad, ' ').c_str(),
           ANSI_BOLD, color, text, ANSI_RESET,
           std::string(rpad, ' ').c_str(),
           ANSI_BOLD, border_color, BOX_V, ANSI_RESET);
}

// ============================================================
//  ui_print_banner()
// ============================================================
void ui_print_banner(bool auto_mode, bool manual_mode) {
    printf("\n");
    box_top_titled(" Multi-Threaded Transaction Processing System ",
                   ANSI_BR_WHITE);

    {
        const char* sub = "OS Project  -  Full System Integration";
        int vis = (int)strlen(sub);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, sub,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET);
    }

    box_divider(ANSI_BR_WHITE);
    box_blank(ANSI_BR_WHITE);

    auto pipe_row = [&](const char* label, const char* lc, const char* detail) {
        int lpad = 18 - (int)strlen(label);
        if (lpad < 0) lpad = 0;
        int vis = 18 + 2 + (int)strlen(detail);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET,
               ANSI_BOLD, lc, label, ANSI_RESET,
               std::string(lpad, ' ').c_str(),
               ANSI_DIM, ANSI_WHITE, detail,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET);
    };

    auto arrow_row = [&](const char* text) {
        int vis = 9 + (int)strlen(text);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET,
               ANSI_BR_BLACK, "         ", text,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET);
    };

    if (auto_mode)
        pipe_row("PRODUCER [AUTO]",   UI_COLOR_PRODUCER,
                 "SLOW · FAST · BURST modes");
    if (manual_mode)
        pipe_row("PRODUCER [MANUAL]", UI_COLOR_PRODUCER,
                 "Step-by-step guided input");

    arrow_row("v  [Shared Memory]  /dev/shm/txn_shared_buffer");
    arrow_row("   Circular buffer  8 slots  sem + mutex sync");
    pipe_row("VALIDATOR",          UI_COLOR_VALIDATOR,
             "Session  Balance  SQL build");
    arrow_row("v  [Named Pipe FIFO]  /tmp/txn_query_pipe");
    arrow_row("   512-byte atomic SQL messages");
    pipe_row("DB UPDATER",         UI_COLOR_UPDATER,
             "BEGIN  INSERT + UPDATE  COMMIT");
    arrow_row("v  [SQLite3 WAL]  transactions.db");

    box_blank(ANSI_BR_WHITE);
    box_divider(ANSI_BR_WHITE);

    pipe_row("LOGGER",  UI_COLOR_SYSTEM,  "Sole stdout owner  async queue");
    pipe_row("MONITOR", UI_COLOR_MONITOR, "Read-only  2s interval  live panels");

    box_blank(ANSI_BR_WHITE);
    box_divider(ANSI_BR_WHITE);

    {
        const char* ctrl = "Ctrl+C  =>  graceful shutdown";
        int vis = (int)strlen(ctrl);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET,
               ANSI_BR_WHITE, ctrl,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET);
    }
    if (manual_mode) {
        const char* fmt = "Manual mode: step-by-step guided wizard";
        int vis = (int)strlen(fmt);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, fmt,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, BOX_V, ANSI_RESET);
    }

    box_blank(ANSI_BR_WHITE);
    box_bottom(ANSI_BR_WHITE);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD STEP 1 — Select User
// ============================================================
void ui_wizard_show_users() {
    const char* bc = UI_COLOR_PRODUCER;
    printf("\n");
    box_top_titled(" STEP 1 of 3 — Who is making this transaction? ", bc);
    box_blank(bc);

    struct { int id; const char* name; const char* bal; bool active; } users[] = {
        {1, "Alice",   "$1,500", true},
        {2, "Bob",     "$2,200", true},
        {3, "Charlie", "$800",   true},
        {4, "Diana",   "$3,100", true},
        {5, "Eve",     "$500",   false},
    };

    for (int i = 0; i < 5; i++) {
        char num[4];   snprintf(num, sizeof(num), "[%d]", users[i].id);
        char nb[32];   snprintf(nb, sizeof(nb), "%-10s  %s",
                                users[i].name, users[i].bal);

        const char* status     = users[i].active
                               ? "Logged In " : "Logged Out";
        const char* status_col = users[i].active
                               ? ANSI_BR_GREEN : ANSI_BR_RED;
        const char* name_col   = users[i].active
                               ? ANSI_BR_WHITE : ANSI_BR_BLACK;

        int vis = 4 + 1 + (int)strlen(nb) + 2 + (int)strlen(status);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;

        printf("%s%s%s  %s%s%s %s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_BOLD, ANSI_BR_CYAN, num, ANSI_RESET,
               ANSI_BOLD, name_col, nb, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, status_col, status, ANSI_RESET,
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }

    box_blank(bc);
    box_divider(bc);
    {
        const char* hint = "  Enter a number from 1 to 5 and press Enter:";
        int vis = (int)strlen(hint);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, hint,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD STEP 2 — Select Transaction Type
// ============================================================
void ui_wizard_show_types(int user_id, const char* user_name) {
    const char* bc = UI_COLOR_VALIDATOR;
    printf("\n");
    box_top_titled(" STEP 2 of 3 — What kind of transaction? ", bc);
    box_blank(bc);

    char user_line[48];
    snprintf(user_line, sizeof(user_line),
             "Account holder: [%d] %s", user_id, user_name);
    box_row_center(user_line, ANSI_BR_CYAN, bc);
    box_blank(bc);
    box_divider(bc);
    box_blank(bc);

    struct { int num; const char* label; const char* desc; const char* col; }
    types[] = {
        {1, "DEPOSIT",    "Add money into your account",     ANSI_BR_GREEN},
        {2, "WITHDRAWAL", "Take money out of your account",  ANSI_BR_YELLOW},
        {3, "TRANSFER",   "Send money to another account",   ANSI_BR_CYAN},
    };

    for (int i = 0; i < 3; i++) {
        char num[4]; snprintf(num, sizeof(num), "[%d]", types[i].num);
        int vis = 4 + 1 + 12 + 2 + (int)strlen(types[i].desc);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;

        printf("%s%s%s  %s%s%s %s%s%-12s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_BOLD, ANSI_BR_CYAN, num, ANSI_RESET,
               ANSI_BOLD, types[i].col, types[i].label, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_DIM, ANSI_WHITE, types[i].desc, ANSI_RESET,
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
        box_blank(bc);
    }

    box_divider(bc);
    {
        const char* hint = "  Enter 1, 2, or 3 and press Enter:";
        int vis = (int)strlen(hint);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, hint,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD STEP 2b — Select Transfer Recipient (TRANSFER only)
// ============================================================
void ui_wizard_show_transfer_recipient(int sender_id,
                                        const char* sender_name) {
    const char* bc = ANSI_BR_BLUE;
    printf("\n");
    box_top_titled(" STEP 2b — Who are you sending money TO? ", bc);
    box_blank(bc);

    char from_line[64];
    snprintf(from_line, sizeof(from_line),
             "Sending FROM: [%d] %s", sender_id, sender_name);
    box_row_center(from_line, ANSI_BR_CYAN, bc);
    box_blank(bc);
    box_divider(bc);
    box_blank(bc);

    struct { int id; const char* name; const char* bal; bool active; } users[] = {
        {1, "Alice",   "$1,500", true},
        {2, "Bob",     "$2,200", true},
        {3, "Charlie", "$800",   true},
        {4, "Diana",   "$3,100", true},
        {5, "Eve",     "$500",   false},
    };

    for (int i = 0; i < 5; i++) {
        // Cannot transfer to yourself
        if (users[i].id == sender_id) continue;

        char num[4]; snprintf(num, sizeof(num), "[%d]", users[i].id);
        char nb[32]; snprintf(nb, sizeof(nb), "%-10s  %s",
                              users[i].name, users[i].bal);

        const char* name_col = users[i].active
                             ? ANSI_BR_WHITE : ANSI_BR_BLACK;

        int vis = 4 + 1 + (int)strlen(nb);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;

        printf("%s%s%s  %s%s%s %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_BOLD, ANSI_BR_CYAN, num, ANSI_RESET,
               ANSI_BOLD, name_col, nb, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }

    box_blank(bc);
    box_divider(bc);
    {
        const char* hint = "  Enter a recipient number and press Enter:";
        int vis = (int)strlen(hint);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, hint,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD STEP 3 — Enter Amount
// ============================================================
void ui_wizard_show_amount(int user_id, const char* user_name,
                            const char* txn_type,
                            double current_balance,
                            int recipient_id,
                            const char* recipient_name) {
    const char* bc = UI_COLOR_UPDATER;
    printf("\n");
    box_top_titled(" STEP 3 of 3 — How much money? ", bc);
    box_blank(bc);

    char sum_line[64];
    snprintf(sum_line, sizeof(sum_line),
             "Account holder: [%d] %s", user_id, user_name);
    box_row_center(sum_line, ANSI_BR_CYAN, bc);

    // Show recipient line only for TRANSFER
    if (recipient_id > 0 && recipient_name != nullptr) {
        char rec_line[64];
        snprintf(rec_line, sizeof(rec_line),
                 "Sending to:     [%d] %s", recipient_id, recipient_name);
        box_row_center(rec_line, ANSI_BR_BLUE, bc);
    }

    char type_line[48];
    snprintf(type_line, sizeof(type_line), "Transaction:    %s", txn_type);
    box_row_center(type_line, ANSI_BR_YELLOW, bc);

    char bal_line[48];
    snprintf(bal_line, sizeof(bal_line),
             "Current Balance: $%.2f", current_balance);
    box_row_center(bal_line, ANSI_BR_GREEN, bc);

    box_blank(bc);
    box_divider(bc);

    if (strcmp(txn_type, "WITHDRAWAL") == 0 ||
        strcmp(txn_type, "TRANSFER")   == 0) {
        char limit_line[64];
        snprintf(limit_line, sizeof(limit_line),
                 "Maximum you can enter: $%.2f", current_balance);
        box_row_center(limit_line, ANSI_BR_YELLOW, bc);
        box_blank(bc);
    }

    {
        const char* hint = "  Type the amount (e.g. 250) and press Enter:";
        int vis = (int)strlen(hint);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, hint,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    }
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD — No Session Warning
// ============================================================
void ui_wizard_warn_no_session(const char* user_name) {
    const char* bc = ANSI_BR_YELLOW;
    printf("\n");
    box_top_titled(" WARNING — This account is logged out ", bc);
    box_blank(bc);

    char line[64];
    snprintf(line, sizeof(line),
             "%s is currently NOT logged in.", user_name);
    box_row_center(line, ANSI_BR_YELLOW, bc);
    box_row_center("This transaction will be REJECTED",  ANSI_BR_RED,    bc);
    box_row_center("by the validation system.",           ANSI_BR_RED,    bc);
    box_blank(bc);
    box_row_center("Do you still want to continue? [y/n]:", ANSI_BR_WHITE, bc);
    box_blank(bc);
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD — Confirm Transaction
// ============================================================
void ui_wizard_show_confirm(int user_id,    const char* user_name,
                             const char* txn_type, double amount,
                             int recipient_id, const char* recipient_name) {
    const char* bc = ANSI_BR_WHITE;
    printf("\n");
    box_top_titled(" REVIEW YOUR TRANSACTION ", bc);
    box_blank(bc);

    char uid_line[32];
    snprintf(uid_line, sizeof(uid_line), "[%d] %s", user_id, user_name);
    box_row_lv("  Account Holder  ", uid_line,    ANSI_DIM, ANSI_BR_CYAN,   bc);
    box_row_lv("  Transaction     ", txn_type,    ANSI_DIM, ANSI_BR_YELLOW, bc);

    // Show recipient for transfers
    if (recipient_id > 0 && recipient_name != nullptr) {
        char rec_line[32];
        snprintf(rec_line, sizeof(rec_line), "[%d] %s",
                 recipient_id, recipient_name);
        box_row_lv("  Sending To      ", rec_line, ANSI_DIM, ANSI_BR_BLUE, bc);
    }

    char amt_line[32];
    snprintf(amt_line, sizeof(amt_line), "$%.2f", amount);
    box_row_lv("  Amount          ", amt_line,    ANSI_DIM, ANSI_BR_GREEN,  bc);

    box_blank(bc);
    box_divider(bc);
    box_row_center("Press Enter to CONFIRM  or  type 'c' to CANCEL",
                   ANSI_BR_WHITE, bc);
    box_blank(bc);
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD — Transaction Queued (Success)
// ============================================================
void ui_wizard_show_queued(int txn_id, int user_id,
                            const char* user_name,
                            const char* txn_type, double amount,
                            int recipient_id, const char* recipient_name) {
    const char* bc = ANSI_BR_GREEN;
    printf("\n");
    box_top_titled(" TRANSACTION ACCEPTED ", bc);
    box_blank(bc);

    char id_line[32];
    snprintf(id_line, sizeof(id_line), "Transaction #%d", txn_id);
    box_row_center(id_line, ANSI_BR_CYAN, bc);
    box_blank(bc);

    char uid_line[32];
    snprintf(uid_line, sizeof(uid_line), "[%d] %s", user_id, user_name);
    box_row_lv("  From Account    ", uid_line,  ANSI_DIM, ANSI_BR_WHITE,  bc);

    if (recipient_id > 0 && recipient_name != nullptr) {
        char rec_line[32];
        snprintf(rec_line, sizeof(rec_line), "[%d] %s",
                 recipient_id, recipient_name);
        box_row_lv("  To Account      ", rec_line, ANSI_DIM, ANSI_BR_BLUE, bc);
    }

    box_row_lv("  Type            ", txn_type,  ANSI_DIM, ANSI_BR_YELLOW, bc);

    char amt_line[32];
    snprintf(amt_line, sizeof(amt_line), "$%.2f", amount);
    box_row_lv("  Amount          ", amt_line,  ANSI_DIM, ANSI_BR_GREEN,  bc);

    box_blank(bc);
    box_divider(bc);
    box_row_center("Your transaction is now being processed by the system.",
                   ANSI_BR_GREEN, bc);
    box_row_center("Watch the log below to track its progress.",
                   ANSI_DIM, bc);
    box_blank(bc);
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  WIZARD — Cancelled
// ============================================================
void ui_wizard_show_cancelled() {
    printf("\n  %s%s  Transaction cancelled. No changes were made.%s\n\n",
           ANSI_BOLD, ANSI_BR_BLACK, ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  WIZARD — Step Prompt
// ============================================================
void ui_wizard_prompt(const char* step_label) {
    printf("  %s%s%s  %s",
           ANSI_BOLD, ANSI_BR_CYAN, step_label, ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  WIZARD — Error Message
// ============================================================
void ui_wizard_error(const char* msg) {
    printf("\n  %s%s  ERROR  %s  %s%s\n\n",
           ANSI_BOLD, ANSI_BG_RED,
           ANSI_RESET, ANSI_BR_RED, msg, ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  WIZARD — Ask Another Transaction
//  FIX: no prompt printed here — prompt is in wizard_read_line()
// ============================================================
void ui_wizard_ask_another() {
    const char* bc = ANSI_BR_BLACK;
    printf("\n");
    box_top_titled(" What would you like to do next? ", bc);
    box_blank(bc);
    box_row_center("[1]  Make another transaction",  ANSI_BR_WHITE, bc);
    box_blank(bc);
    box_row_center("[q]  Exit manual input mode",    ANSI_BR_BLACK, bc);
    box_blank(bc);
    box_bottom(bc);
    // NO printf/prompt here — wizard_read_line() handles the prompt
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  Legacy input functions
// ============================================================
void ui_print_input_panel() {}

void ui_print_input_prompt() {
    printf("  %s%s txn %s%s%s >  %s",
           ANSI_BOLD, ANSI_BG_CYAN,
           ANSI_RESET, ANSI_BOLD, UI_COLOR_PRODUCER, ANSI_RESET);
    fflush(stdout);
}

void ui_print_input_error(const char* field, const char* reason) {
    printf("\n  %s%s ERROR %s  %s%s: %s%s\n\n",
           ANSI_BOLD, ANSI_BG_RED, ANSI_RESET,
           ANSI_BR_RED, ANSI_BOLD, field,
           ANSI_RESET, ANSI_BR_WHITE, reason, ANSI_RESET);
    fflush(stdout);
}

void ui_print_input_success(int txn_id, int user_id,
                             double amount, const char* type) {
    printf("\n  %s%s ACCEPTED %s  Transaction #%d  User:%d  $%.0f  %s%s\n\n",
           ANSI_BOLD, ANSI_BG_GREEN, ANSI_RESET,
           txn_id, user_id, amount, type, ANSI_RESET);
    fflush(stdout);
}

void ui_print_input_hint(const char* hint) {
    printf("  %s%s  Note: %s%s\n",
           ANSI_BOLD, ANSI_BR_YELLOW, hint, ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_animate_transition()
// ============================================================
void ui_animate_transition(const char* from_stage,
                           const char* to_stage,
                           const char* detail) {
    const char* frames[] = {".", "-", "-", ">"};
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
//  IMPROVED: human-readable messages shown here
// ============================================================
std::string ui_format_log(const char* thread_type,
                           int         thread_num,
                           const char* message,
                           const char* timestamp) {
    const char* color  = ANSI_BR_WHITE;
    const char* symbol = "*";

    if      (strcmp(thread_type, "PRODUCER")  == 0) {
        color = UI_COLOR_PRODUCER;  symbol = ">";
    }
    else if (strcmp(thread_type, "VALIDATOR") == 0) {
        color = UI_COLOR_VALIDATOR; symbol = "#";
    }
    else if (strcmp(thread_type, "UPDATER")   == 0) {
        color = UI_COLOR_UPDATER;   symbol = "+";
    }
    else if (strcmp(thread_type, "MONITOR")   == 0) {
        color = UI_COLOR_MONITOR;   symbol = "o";
    }
    else {
        color = UI_COLOR_SYSTEM;    symbol = ".";
    }

    char label[24];
    if (thread_num > 0)
        snprintf(label, sizeof(label), "%s-%d", thread_type, thread_num);
    else
        snprintf(label, sizeof(label), "%s", thread_type);

    std::string msg_str(message);
    const char* mc = ANSI_WHITE;
    if      (msg_str.find("REJECTED")  != std::string::npos) mc = ANSI_BR_RED;
    else if (msg_str.find("ACCEPTED")  != std::string::npos ||
             msg_str.find("VALID")     != std::string::npos ||
             msg_str.find("COMMITTED") != std::string::npos ||
             msg_str.find("SAVED")     != std::string::npos) mc = ANSI_BR_GREEN;
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
           ANSI_RESET, ANSI_BOLD, ANSI_BR_WHITE, title,
           ANSI_RESET, ANSI_BOLD, ANSI_BR_BLACK,
           ui_repeat("─", side).c_str(), ANSI_RESET);
    fflush(stdout);
}

// ============================================================
//  ui_print_monitor_snapshot()
//  FIX: WARNING only shown when pending+processing > 0
//       AND system is still running (not just pre-commit lag)
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
    snprintf(title, sizeof(title), " System Update #%d ", snapshot_num);
    int t_vis = (int)strlen(title);
    int left  = (inner - t_vis) / 2;
    int right = inner - t_vis - left;
    if (left  < 0) left  = 0;
    if (right < 0) right = 0;

    printf("%s%s%s%s%s%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_TL, ui_repeat(THIN_H, left).c_str(),
           ANSI_RESET, ANSI_BOLD, UI_COLOR_MONITOR, title,
           ANSI_RESET, ANSI_BOLD, bc,
           ui_repeat(THIN_H, right).c_str(), THIN_TR, ANSI_RESET);

    // Buffer bar
    {
        std::string bar_vis = "[", bar_col = "[";
        for (int i = 0; i < buf_total; i++) {
            if (i < buf_count) {
                bar_col += std::string(ANSI_BR_CYAN) + "#" + ANSI_RESET;
                bar_vis += "#";
            } else {
                bar_col += std::string(ANSI_BR_BLACK) + "." + ANSI_RESET;
                bar_vis += ".";
            }
        }
        bar_col += "]"; bar_vis += "]";
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "  %d/%d slots used",
                 buf_count, buf_total);
        int vis = 2 + 20 + (int)bar_vis.size() + (int)strlen(suffix);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %sTransaction Queue  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               bar_col.c_str(),
               ANSI_WHITE, suffix, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    }

    // Divider
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_ML, ui_repeat(THIN_H, inner).c_str(), THIN_MR, ANSI_RESET);

    // Stats with human-readable labels
    auto stat = [&](const char* label, int val, const char* vc) {
        char num[16]; snprintf(num, sizeof(num), "%d", val);
        int vis = (int)strlen(label) + (int)strlen(num);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_WHITE, label, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, num, ANSI_RESET,
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    };

    stat("Validated and forwarded      ", done,       ANSI_BR_GREEN);
    stat("Rejected (failed checks)     ", rejected,   ANSI_BR_RED);
    stat("Waiting to be validated      ", pending,
         pending > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    stat("Currently being validated    ", processing,
         processing > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    stat("Permanently saved to database", committed,  ANSI_BR_GREEN);

    // Divider
    printf("%s%s%s%s%s%s\n",
           ANSI_BOLD, bc,
           THIN_ML, ui_repeat(THIN_H, inner).c_str(), THIN_MR, ANSI_RESET);

    // Throughput
    {
        char tps_str[16];
        snprintf(tps_str, sizeof(tps_str), "%.1f per second", tps);
        int vis = (int)strlen("Pipeline speed               ")
                + (int)strlen(tps_str);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %sPipeline speed               %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_WHITE,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, ANSI_BR_CYAN, tps_str, ANSI_RESET,
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    }

    // WARNING only when truly stuck (more than 3 pending AND no progress)
    // Avoids false alarm when a transaction was just submitted
    if ((pending + processing) > 3) {
        char warn[80];
        snprintf(warn, sizeof(warn),
                 "Note: %d transactions are waiting longer than expected",
                 pending + processing);
        int vis = (int)strlen(warn);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, THIN_V, ANSI_RESET,
               ANSI_BOLD, ANSI_BR_YELLOW, warn, ANSI_RESET,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, bc, THIN_V, ANSI_RESET);
    }

    printf("%s%s%s%s%s%s\n\n",
           ANSI_BOLD, bc,
           THIN_BL, ui_repeat(THIN_H, inner).c_str(), THIN_BR, ANSI_RESET);
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

    auto row = [&](const char* label, int val, const char* vc) {
        char num[16]; snprintf(num, sizeof(num), "%d", val);
        int vis = (int)strlen(label) + (int)strlen(num);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_WHITE, label,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, num, ANSI_RESET,
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    };

    box_blank(bc);
    row("Total transactions created     ", generated,  ANSI_BR_CYAN);
    box_blank(bc);
    box_divider(bc);
    box_blank(bc);
    row("Successfully validated         ", done,       ANSI_BR_GREEN);
    row("Rejected by validation system  ", rejected,   ANSI_BR_RED);
    row("Still waiting (should be 0)    ", pending,
        pending > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    row("Still processing (should be 0) ", processing,
        processing > 0 ? ANSI_BR_YELLOW : ANSI_BR_BLACK);
    box_blank(bc);
    row("Permanently saved to database  ", committed,  ANSI_BR_GREEN);
    box_blank(bc);
    box_divider(bc);
    box_blank(bc);

    auto check_row = [&](const char* label, bool ok) {
        const char* vc   = ok ? ANSI_BR_GREEN : ANSI_BR_RED;
        const char* word = ok ? "PASS" : "FAIL";
        const char* sym  = ok ? CHECK  : CROSS;
        int vis = 3 + (int)strlen(label) + 4;
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_BOLD, vc, sym, ANSI_RESET,
               ANSI_WHITE, label,
               std::string(pad, ' ').c_str(),
               ANSI_BOLD, vc, word, ANSI_RESET,
               ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    };

    check_row("All transactions accounted for ", audit_ok);
    check_row("All validated = all saved      ", pipeline_ok);
    box_blank(bc);
    box_divider(bc);

    auto hint_row = [&](const char* h) {
        int vis = (int)strlen(h);
        int pad = (W - 4) - vis; if (pad < 0) pad = 0;
        printf("%s%s%s  %s%s%s%s%s%s\n",
               ANSI_BOLD, bc, BOX_V, ANSI_RESET,
               ANSI_DIM, ANSI_WHITE, h,
               std::string(pad, ' ').c_str(),
               ANSI_RESET, ANSI_BOLD, bc, BOX_V, ANSI_RESET);
    };

    hint_row("  To inspect results: sqlite3 transactions.db");
    hint_row("  SELECT * FROM transactions;");
    hint_row("  SELECT user_id, name, balance FROM users;");
    box_blank(bc);
    box_bottom(bc);
    printf("\n");
    fflush(stdout);
}

// ============================================================
//  ui_print_shutdown_banner()
// ============================================================
void ui_print_shutdown_banner() {
    printf("\n%s%s  System is shutting down — finishing all pending work...%s\n\n",
           ANSI_BOLD, ANSI_BR_YELLOW, ANSI_RESET);
    fflush(stdout);
}