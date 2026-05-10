#include "ui.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <ncurses.h>
#include <string>
#include <vector>

std::mutex g_ui_mutex;

// ── Layout ───────────────────────────────────────────────────
// Row 0-1      : Header bar
// Row 2..TOP   : Pipeline Status (left) | Buffer Queue (right)
// Row TOP..MID : Metrics (left)         | Event Log (right)
// Row MID..H-2 : Transaction History (full width)
// Row H-1      : Footer hints
static const int HEADER_H = 2;
static const int TOP_H = 11;
static const int MID_H = 8;
static const int FOOTER_H = 1;

static int S_ROWS = 0, S_COLS = 0;
static int HIST_START = 0;

static WINDOW *w_header = nullptr;
static WINDOW *w_producers = nullptr;
static WINDOW *w_validators = nullptr;
static WINDOW *w_validators_in = nullptr;
static WINDOW *w_updaters = nullptr;
static WINDOW *w_updaters_in = nullptr;
static WINDOW *w_metrics = nullptr;
static WINDOW *w_queue = nullptr;
static WINDOW *w_events = nullptr;
static WINDOW *w_events_in = nullptr;
static WINDOW *w_history = nullptr;
static WINDOW *w_history_in = nullptr;
static WINDOW *w_footer = nullptr;
static WINDOW *w_wizard = nullptr;

// ── Per-thread status storage ────────────────────────────────
static char g_prod_st[4][72] = {};
static char g_val_st[2][72] = {};
static char g_upd_st[2][72] = {};

// ── Queue entries ─────────────────────────────────────────────
struct QEntry {
  int id;
  char user[20];
  char type[12];
  double amount;
};
static QEntry g_qbuf[32];
static int g_qcount = 0;

// ── History rows ─────────────────────────────────────────────
struct HEntry {
  int id;
  char type[12];
  double amount;
  char status[8];
  char ts[10];
};
static std::vector<HEntry> g_hist;

static int g_current_buf_count = 0;

// ── Helpers ──────────────────────────────────────────────────
static void titled_box(WINDOW *w, const char *title, int cp) {
  wattron(w, COLOR_PAIR(CP_BORDER));
  box(w, 0, 0);
  wattroff(w, COLOR_PAIR(CP_BORDER));
  if (title) {
    wattron(w, COLOR_PAIR(cp) | A_BOLD);
    mvwprintw(w, 0, 2, " %s ", title);
    wattroff(w, COLOR_PAIR(cp) | A_BOLD);
  }
}

// ── Redraw Thread Sections ────────────────────────────────────
static void redraw_producers() {
  if (!w_producers)
    return;
  werase(w_producers);
  titled_box(w_producers, "PRODUCERS", CP_PRODUCER);
  for (int i = 0; i < 4; i++) {
    if (!g_prod_st[i][0])
      continue;
    wattron(w_producers, COLOR_PAIR(CP_PRODUCER));
    mvwprintw(w_producers, i + 1, 2, "P-%d: %s", i + 1, g_prod_st[i]);
    wattroff(w_producers, COLOR_PAIR(CP_PRODUCER));
  }
  wrefresh(w_producers);
}

static void redraw_validators() {
  if (!w_validators)
    return;
  werase(w_validators);
  titled_box(w_validators, "VALIDATORS", CP_VALIDATOR);
  for (int i = 0; i < 2; i++) {
    if (!g_val_st[i][0])
      continue;
    wattron(w_validators, COLOR_PAIR(CP_VALIDATOR));
    mvwprintw(w_validators, i + 1, 2, "V-%d: %s", i + 1, g_val_st[i]);
    wattroff(w_validators, COLOR_PAIR(CP_VALIDATOR));
  }
  wrefresh(w_validators);
}

static void redraw_updaters() {
  if (!w_updaters)
    return;
  werase(w_updaters);
  titled_box(w_updaters, "UPDATERS", CP_UPDATER);
  for (int i = 0; i < 2; i++) {
    if (!g_upd_st[i][0])
      continue;
    wattron(w_updaters, COLOR_PAIR(CP_UPDATER));
    mvwprintw(w_updaters, i + 1, 2, "U-%d: %s", i + 1, g_upd_st[i]);
    wattroff(w_updaters, COLOR_PAIR(CP_UPDATER));
  }
  wrefresh(w_updaters);
}

// ── Redraw queue panel ────────────────────────────────────────
static void redraw_queue() {
  if (!w_queue)
    return;
  werase(w_queue);

  char title[48];
  snprintf(title, sizeof(title), "BUFFER QUEUE  Live: %d slots",
           g_current_buf_count);
  titled_box(w_queue, title, CP_PRODUCER);

  // Visual bar at top (show live count vs 8 slots)
  wattron(w_queue, COLOR_PAIR(CP_SYSTEM));
  mvwprintw(w_queue, 1, 2, "[");
  for (int i = 0; i < 8; i++) {
    if (i < g_current_buf_count) {
      wattron(w_queue, COLOR_PAIR(CP_PRODUCER) | A_BOLD);
      waddch(w_queue, '#');
      wattroff(w_queue, COLOR_PAIR(CP_PRODUCER) | A_BOLD);
    } else {
      wattron(w_queue, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      waddch(w_queue, '.');
      wattroff(w_queue, COLOR_PAIR(CP_SYSTEM) | A_DIM);
    }
  }
  wattron(w_queue, COLOR_PAIR(CP_SYSTEM));
  wprintw(w_queue, "] %d/8", g_current_buf_count);
  wattroff(w_queue, COLOR_PAIR(CP_SYSTEM));

  int max_r = getmaxy(w_queue) - 1;
  int start = g_qcount > (max_r - 2) ? g_qcount - (max_r - 2) : 0;
  for (int i = start, r = 2; i < g_qcount && r < max_r; i++, r++) {
    QEntry &e = g_qbuf[i % 32];
    wattron(w_queue, COLOR_PAIR(CP_VALIDATOR));
    mvwprintw(w_queue, r, 2, "#%-4d %-10s %-10s $%.0f", e.id, e.user, e.type,
              e.amount);
    wattroff(w_queue, COLOR_PAIR(CP_VALIDATOR));
  }
  wrefresh(w_queue);
}

// ────────────────────────────────────────────────────────────
void ui_init() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  start_color();
  use_default_colors();

  init_pair(CP_HEADER, COLOR_WHITE, COLOR_CYAN);
  init_pair(CP_PRODUCER, COLOR_CYAN, -1);
  init_pair(CP_VALIDATOR, COLOR_YELLOW, -1);
  init_pair(CP_UPDATER, COLOR_GREEN, -1);
  init_pair(CP_MONITOR, COLOR_MAGENTA, -1);
  init_pair(CP_SYSTEM, COLOR_WHITE, -1);
  init_pair(CP_ERROR, COLOR_RED, -1);
  init_pair(CP_SUCCESS, COLOR_GREEN, -1);
  init_pair(CP_BORDER, COLOR_CYAN, -1);
  init_pair(CP_FOOTER, COLOR_BLACK, COLOR_WHITE);

  getmaxyx(stdscr, S_ROWS, S_COLS);
  int half = S_COLS / 2;

  // Top-Left stacking (Producers, Validators, Updaters)
  int p_h = 5;
  int v_h = 11;
  int u_h = 11;

  // Top-Right stacking (Queue, Event Log)
  int q_h = 10;
  int e_h = 17;

  // Bottom section height (History and Metrics)
  int b_h = S_ROWS - (HEADER_H + p_h + v_h + u_h) - FOOTER_H;
  if (b_h < 6)
    b_h = 6;

  w_header = newwin(HEADER_H, S_COLS, 0, 0);

  w_producers = newwin(p_h, half, HEADER_H, 0);
  w_validators = newwin(v_h, half, HEADER_H + p_h, 0);
  w_validators_in = derwin(w_validators, v_h - 2, half - 4, 1, 2);

  w_updaters = newwin(u_h, half, HEADER_H + p_h + v_h, 0);
  w_updaters_in = derwin(w_updaters, u_h - 2, half - 4, 1, 2);

  w_queue = newwin(q_h, S_COLS - half, HEADER_H, half);
  w_events = newwin(e_h, S_COLS - half, HEADER_H + q_h, half);
  w_events_in = derwin(w_events, e_h - 2, S_COLS - half - 4, 1, 2);

  HIST_START = HEADER_H + p_h + v_h + u_h;
  w_history = newwin(b_h, half, HIST_START, 0);
  w_history_in = derwin(w_history, b_h - 3, half - 4, 2, 2);

  w_metrics = newwin(b_h, S_COLS - half, HIST_START, half);

  w_footer = newwin(FOOTER_H, S_COLS, S_ROWS - FOOTER_H, 0);

  scrollok(w_events_in, TRUE);
  scrollok(w_history_in, TRUE);
  scrollok(w_validators_in, TRUE);
  scrollok(w_updaters_in, TRUE);

  // Initial borders - Force draw on parent windows
  redraw_producers();
  titled_box(w_validators, "VALIDATORS", CP_VALIDATOR);
  titled_box(w_updaters, "UPDATERS", CP_UPDATER);
  titled_box(w_queue, "BUFFER QUEUE", CP_PRODUCER);
  titled_box(w_events, "EVENT LOG", CP_VALIDATOR);
  titled_box(w_history, "TRANSACTION HISTORY", CP_UPDATER);
  titled_box(w_metrics, "SYSTEM METRICS", CP_MONITOR);

  // Proper multi-window refresh
  touchwin(stdscr);
  wnoutrefresh(w_header);
  wnoutrefresh(w_producers);
  wnoutrefresh(w_validators);
  wnoutrefresh(w_updaters);
  wnoutrefresh(w_queue);
  wnoutrefresh(w_events);
  wnoutrefresh(w_history);
  wnoutrefresh(w_metrics);
  wnoutrefresh(w_footer);
  doupdate();

  // History column headers
  wattron(w_history_in, COLOR_PAIR(CP_SYSTEM) | A_BOLD | A_UNDERLINE);
  mvwprintw(w_history_in, 0, 0, "%-6s %-12s %10s", "TXN#", "TYPE", "AMOUNT");
  wattroff(w_history_in, COLOR_PAIR(CP_SYSTEM) | A_BOLD | A_UNDERLINE);

  touchwin(w_history);
  wnoutrefresh(w_history_in);
  doupdate();

  // Footer
  wbkgd(w_footer, COLOR_PAIR(CP_FOOTER));
  wattron(w_footer, COLOR_PAIR(CP_FOOTER) | A_BOLD);
  mvwprintw(w_footer, 0, 1,
            "Ctrl+C: Stop  |  P=Producers  V=Validators  U=Updaters  |  "
            "Auto-refreshes every 2s");
  wattroff(w_footer, COLOR_PAIR(CP_FOOTER) | A_BOLD);
  wrefresh(w_footer);

  refresh();
}

void ui_shutdown() {
  if (w_header)
    delwin(w_header);
  if (w_producers)
    delwin(w_producers);
  if (w_validators_in)
    delwin(w_validators_in);
  if (w_validators)
    delwin(w_validators);
  if (w_updaters_in)
    delwin(w_updaters_in);
  if (w_updaters)
    delwin(w_updaters);
  if (w_metrics)
    delwin(w_metrics);
  if (w_events_in)
    delwin(w_events_in);
  if (w_events)
    delwin(w_events);
  if (w_history_in)
    delwin(w_history_in);
  if (w_history)
    delwin(w_history);
  if (w_footer)
    delwin(w_footer);
  if (w_wizard)
    delwin(w_wizard);
  endwin();
}

// ── Header ───────────────────────────────────────────────────
void ui_update_header(bool auto_mode, bool manual_mode) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  if (!w_header)
    return;
  wbkgd(w_header, COLOR_PAIR(CP_HEADER));
  werase(w_header);
  wattron(w_header, COLOR_PAIR(CP_HEADER) | A_BOLD);

  mvwprintw(w_header, 0, 1, "TRANSACTION PROCESSING SYSTEM");
  wprintw(w_header, "   |");
  if (auto_mode)
    wprintw(w_header, "  Auto");
  if (manual_mode)
    wprintw(w_header, "  Manual");
  wprintw(w_header, "  |  Threads: 3 producers  2 validators  2 updaters");

  // Live clock right-aligned
  time_t now = time(nullptr);
  char ts[16];
  strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
  mvwprintw(w_header, 0, S_COLS - 10, "%s", ts);

  wattroff(w_header, COLOR_PAIR(CP_HEADER) | A_BOLD);
  wrefresh(w_header);
}

// ── Pipeline status ───────────────────────────────────────────
void ui_set_thread_status(const char *type, int num, const char *status) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  int idx = num - 1;

  time_t now = time(nullptr);
  char ts[10];
  strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));

  if (strcmp(type, "PRODUCER") == 0 && idx >= 0 && idx < 4) {
    snprintf(g_prod_st[idx], 72, "%s", status);
    redraw_producers();
  } else if (strcmp(type, "VALIDATOR") == 0 && idx >= 0 && idx < 2) {
    if (w_validators_in) {
      wattron(w_validators_in, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      wprintw(w_validators_in, "[%s] ", ts);
      wattroff(w_validators_in, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      wattron(w_validators_in, COLOR_PAIR(CP_VALIDATOR) | A_BOLD);
      wprintw(w_validators_in, "V-%d: ", num);
      wattroff(w_validators_in, COLOR_PAIR(CP_VALIDATOR) | A_BOLD);
      wattron(w_validators_in, COLOR_PAIR(CP_VALIDATOR));
      wprintw(w_validators_in, "%s\n", status);
      wattroff(w_validators_in, COLOR_PAIR(CP_VALIDATOR));

      touchwin(w_validators);
      wnoutrefresh(w_validators);    // Refresh the border
      wnoutrefresh(w_validators_in); // Refresh the content
      doupdate();
    }
  } else if (strcmp(type, "UPDATER") == 0 && idx >= 0 && idx < 2) {
    if (w_updaters_in) {
      wattron(w_updaters_in, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      wprintw(w_updaters_in, "[%s] ", ts);
      wattroff(w_updaters_in, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      wattron(w_updaters_in, COLOR_PAIR(CP_UPDATER) | A_BOLD);
      wprintw(w_updaters_in, "U-%d: ", num);
      wattroff(w_updaters_in, COLOR_PAIR(CP_UPDATER) | A_BOLD);
      wattron(w_updaters_in, COLOR_PAIR(CP_UPDATER));
      wprintw(w_updaters_in, "%s\n", status);
      wattroff(w_updaters_in, COLOR_PAIR(CP_UPDATER));

      touchwin(w_updaters);
      wnoutrefresh(w_updaters);    // Refresh the border
      wnoutrefresh(w_updaters_in); // Refresh the content
      doupdate();
    }
  }
}

// ── Queue push ────────────────────────────────────────────────
void ui_queue_push(int txn_id, const char *user_name, const char *type,
                   double amount) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  QEntry &e = g_qbuf[g_qcount % 32];
  e.id = txn_id;
  snprintf(e.user, sizeof(e.user), "%s", user_name);
  snprintf(e.type, sizeof(e.type), "%s", type);
  e.amount = amount;
  g_qcount++;
  g_current_buf_count++; // Increment live count
  if (g_current_buf_count > 8)
    g_current_buf_count = 8;
  redraw_queue();
}

void ui_queue_pop() {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  if (g_current_buf_count > 0)
    g_current_buf_count--;
  redraw_queue();
}

// ── Event log ─────────────────────────────────────────────────
void ui_add_log(const char *thread_type, int thread_num, const char *message,
                const char *timestamp) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  if (!w_events_in)
    return;

  int cp = CP_SYSTEM;
  if (strcmp(thread_type, "PRODUCER") == 0)
    cp = CP_PRODUCER;
  else if (strcmp(thread_type, "VALIDATOR") == 0)
    cp = CP_VALIDATOR;
  else if (strcmp(thread_type, "UPDATER") == 0)
    cp = CP_UPDATER;
  else if (strcmp(thread_type, "MONITOR") == 0)
    cp = CP_MONITOR;

  // Label
  wattron(w_events_in, COLOR_PAIR(cp) | A_BOLD);
  if (thread_num > 0)
    wprintw(w_events_in, "%s-%d: ", thread_type, thread_num);
  else
    wprintw(w_events_in, "%s: ", thread_type);
  wattroff(w_events_in, COLOR_PAIR(cp) | A_BOLD);

  // Message handling: split by newline to avoid wrapping mess
  std::string msg(message);
  size_t pos = 0;
  while ((pos = msg.find('\n')) != std::string::npos) {
    std::string line = msg.substr(0, pos);
    size_t first = line.find_first_not_of(' ');
    if (std::string::npos != first)
      line = line.substr(first);

    if (!line.empty()) {
      wattron(w_events_in, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      wprintw(w_events_in, " > ");
      wattroff(w_events_in, COLOR_PAIR(CP_SYSTEM) | A_DIM);
      wprintw(w_events_in, "%s\n", line.c_str());
    }
    msg.erase(0, pos + 1);
  }

  if (!msg.empty()) {
    int mcp = CP_SYSTEM;
    if (msg.find("REJECTED") != std::string::npos ||
        msg.find("FAIL") != std::string::npos)
      mcp = CP_ERROR;
    else if (msg.find("SAVED") != std::string::npos ||
             msg.find("DONE") != std::string::npos ||
             msg.find("ACCEPTED") != std::string::npos)
      mcp = CP_SUCCESS;

    wattron(w_events_in, COLOR_PAIR(mcp));
    wprintw(w_events_in, "%s\n", msg.c_str());
    wattroff(w_events_in, COLOR_PAIR(mcp));
  }

  touchwin(w_events);
  wnoutrefresh(w_events);    // Refresh the border
  wnoutrefresh(w_events_in); // Refresh the content
  doupdate();
}

// ── Metrics ───────────────────────────────────────────────────
void ui_update_monitor(int snapshot_num, int buf_count, int buf_total,
                       int /*done*/, int rejected, int pending, int processing,
                       int committed, double tps, int deposits, int withdrawals,
                       int transfers) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  // Don't override g_current_buf_count here to keep the live pushes/pops
  if (!w_metrics)
    return;
  werase(w_metrics);
  titled_box(w_metrics, "SYSTEM METRICS", CP_MONITOR);

  redraw_queue(); // Update the queue bar whenever metrics are updated

  wattron(w_metrics, COLOR_PAIR(CP_SUCCESS) | A_BOLD);
  mvwprintw(w_metrics, 1, 2, "TPS         : %.2f", tps);
  wattroff(w_metrics, COLOR_PAIR(CP_SUCCESS) | A_BOLD);

  wattron(w_metrics, COLOR_PAIR(CP_SYSTEM));
  mvwprintw(w_metrics, 2, 2, "Committed   : %d", committed);
  mvwprintw(w_metrics, 3, 2, "Rejected    : %d", rejected);
  mvwprintw(w_metrics, 4, 2, "Pending     : %d", pending);
  mvwprintw(w_metrics, 5, 2, "Processing  : %d", processing);
  wattroff(w_metrics, COLOR_PAIR(CP_SYSTEM));

  wattron(w_metrics, COLOR_PAIR(CP_PRODUCER));
  mvwprintw(w_metrics, 2, getmaxx(w_metrics) / 2, "Deposits    : %d", deposits);
  wattroff(w_metrics, COLOR_PAIR(CP_PRODUCER));
  wattron(w_metrics, COLOR_PAIR(CP_VALIDATOR));
  mvwprintw(w_metrics, 3, getmaxx(w_metrics) / 2, "Withdrawals : %d",
            withdrawals);
  wattroff(w_metrics, COLOR_PAIR(CP_VALIDATOR));
  wattron(w_metrics, COLOR_PAIR(CP_UPDATER));
  mvwprintw(w_metrics, 4, getmaxx(w_metrics) / 2, "Transfers   : %d",
            transfers);
  wattroff(w_metrics, COLOR_PAIR(CP_UPDATER));

  wattron(w_metrics, COLOR_PAIR(CP_SYSTEM) | A_DIM);
  mvwprintw(w_metrics, 6, 2, "Snapshot #%d", snapshot_num);
  wattroff(w_metrics, COLOR_PAIR(CP_SYSTEM) | A_DIM);

  wrefresh(w_metrics);

  // Also refresh header clock
  time_t now = time(nullptr);
  char ts[16];
  strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
  mvwprintw(w_header, 0, S_COLS - 10, "%s", ts);
  wrefresh(w_header);
}

// ── History ───────────────────────────────────────────────────
void ui_history_push(int txn_id, const char *type, double amount, bool saved) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  if (!w_history_in)
    return;

  HEntry e;
  e.id = txn_id;
  snprintf(e.type, sizeof(e.type), "%s", type);
  snprintf(e.status, sizeof(e.status), "%s", saved ? "SAVED" : "FAILED");
  e.amount = amount;
  time_t now = time(nullptr);
  strftime(e.ts, sizeof(e.ts), "%H:%M:%S", localtime(&now));
  g_hist.push_back(e);

  int cp = saved ? CP_SUCCESS : CP_ERROR;
  wattron(w_history_in, COLOR_PAIR(cp));
  wprintw(w_history_in, "  %-6d %-12s %10.2f\n", e.id, e.type, e.amount);
  wattroff(w_history_in, COLOR_PAIR(cp));

  touchwin(w_history);
  wnoutrefresh(w_history);    // Refresh the border
  wnoutrefresh(w_history_in); // Refresh the content
  doupdate();
}

// ── Wizard ────────────────────────────────────────────────────
static void ensure_wizard() {
  if (!w_wizard)
    w_wizard = newwin(TOP_H, S_COLS / 2, HEADER_H, S_COLS / 4);
}

void ui_wizard_clear() {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  ensure_wizard();
  werase(w_wizard);
  titled_box(w_wizard, "MANUAL TRANSACTION WIZARD", CP_HEADER);
  wrefresh(w_wizard);
}

void ui_wizard_print(int row, int col, const char *text, int color_pair) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  ensure_wizard();
  if (color_pair > 0)
    wattron(w_wizard, COLOR_PAIR(color_pair) | A_BOLD);
  mvwprintw(w_wizard, row + 1, col + 2, "%s", text);
  if (color_pair > 0)
    wattroff(w_wizard, COLOR_PAIR(color_pair) | A_BOLD);
  wrefresh(w_wizard);
}

void ui_wizard_get_string(char *buf, int max_len, const char *prompt) {
  {
    std::lock_guard<std::mutex> lk(g_ui_mutex);
    ensure_wizard();
    wattron(w_wizard, COLOR_PAIR(CP_SUCCESS));
    mvwprintw(w_wizard, 3, 2, "%-36s", prompt);
    mvwprintw(w_wizard, 4, 2, "> ");
    wattroff(w_wizard, COLOR_PAIR(CP_SUCCESS));
    wrefresh(w_wizard);
    curs_set(1);
    echo();
  }
  wgetnstr(w_wizard, buf, max_len - 1);
  {
    std::lock_guard<std::mutex> lk(g_ui_mutex);
    noecho();
    curs_set(0);
    wrefresh(w_wizard);
  }
}

// ── Final report ─────────────────────────────────────────────
void ui_show_final_report(int generated, int done, int rejected, int pending,
                          int processing, int committed) {
  printf("\n============================================================\n");
  printf("  FINAL REPORT\n");
  printf("============================================================\n");
  printf("  Generated  : %d\n", generated);
  printf("  Committed  : %d\n", committed);
  printf("  Done       : %d\n", done);
  printf("  Rejected   : %d\n", rejected);
  printf("  Pending    : %d\n", pending);
  printf("  Processing : %d\n", processing);
  printf("============================================================\n\n");
}