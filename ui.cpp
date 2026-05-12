#include "ui.h"
#include <pthread.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <ncurses.h>
#include <string>
#include <thread>
#include <vector>
#include <csignal>

std::mutex g_ui_mutex;

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
static WINDOW *w_queue = nullptr;
static WINDOW *w_events = nullptr;
static WINDOW *w_events_in = nullptr;
static WINDOW *w_history = nullptr;
static WINDOW *w_history_in = nullptr;
static WINDOW *w_footer = nullptr;
static WINDOW *w_wizard = nullptr;

static char g_prod_st[4][72] = {};

struct QEntry {
  int id;
  char user[20];
  char type[12];
  double amount;
};
static QEntry g_qbuf[32];
static int g_qcount = 0;

struct HEntry {
  int id;
  char type[12];
  double amount;
  char status[8];
  char ts[10];
};
static std::vector<HEntry> g_hist;

static int g_current_buf_count = 0;

// Draws a bordered box with a title; internal UI utility.
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

// Redraws parent and child windows while preserving the wizard's visibility.
static void refresh_with_wizard(WINDOW* parent, WINDOW* child) {
    touchwin(parent);
    wnoutrefresh(parent);
    if (child) wnoutrefresh(child);
    
    if (w_wizard) {
        touchwin(w_wizard);
        wnoutrefresh(w_wizard);
    }
    doupdate();
}

// Updates the producer status window with cached thread states.
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

// Updates the visual representation of the shared buffer queue.
static void redraw_queue() {
  if (!w_queue)
    return;
  werase(w_queue);

  char title[48];
  snprintf(title, sizeof(title), "BUFFER QUEUE  Live: %d slots",
           g_current_buf_count);
  titled_box(w_queue, title, CP_PRODUCER);

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
  init_pair(CP_WIZARD, COLOR_WHITE, COLOR_BLUE);

  getmaxyx(stdscr, S_ROWS, S_COLS);
  int half = S_COLS / 2;

  int p_h = 5;
  int v_h = 11;
  int u_h = 11;

  int q_h = 10;

  int b_h = S_ROWS - (HEADER_H + p_h + v_h + u_h) - FOOTER_H;
  if (b_h < 6)
    b_h = 6;

  w_header = newwin(HEADER_H, S_COLS, 0, 0);

  w_producers = newwin(p_h, half, HEADER_H, 0);
  w_validators = newwin(v_h, half, HEADER_H + p_h, 0);
  w_validators_in = derwin(w_validators, v_h - 2, half - 4, 1, 2);

  w_updaters = newwin(u_h, half, HEADER_H + p_h + v_h, 0);
  w_updaters_in = derwin(w_updaters, u_h - 2, half - 4, 1, 2);

  w_queue      = newwin(q_h, S_COLS - half, HEADER_H, half);
    
  int e_h_new = S_ROWS - (HEADER_H + q_h) - FOOTER_H;
  w_events     = newwin(e_h_new, S_COLS - half, HEADER_H + q_h, half);
  w_events_in  = derwin(w_events, e_h_new - 2, S_COLS - half - 4, 1, 2);

  HIST_START   = HEADER_H + p_h + v_h + u_h;
  w_history    = newwin(b_h, half, HIST_START, 0);
  w_history_in = derwin(w_history, b_h - 3, half - 4, 2, 2);
    
  w_footer     = newwin(FOOTER_H, S_COLS, S_ROWS - FOOTER_H, 0);

  scrollok(w_events_in, TRUE);
  scrollok(w_history_in, TRUE);
  scrollok(w_validators_in, TRUE);
  scrollok(w_updaters_in, TRUE);

  redraw_producers();
  titled_box(w_validators, "VALIDATORS", CP_VALIDATOR);
  titled_box(w_updaters, "UPDATERS", CP_UPDATER);
  titled_box(w_queue,      "BUFFER QUEUE",        CP_PRODUCER);
  titled_box(w_events,     "EVENT LOG",           CP_VALIDATOR);
  titled_box(w_history,    "TRANSACTION HISTORY", CP_UPDATER);
    
  touchwin(stdscr);
  wnoutrefresh(w_header);
  wnoutrefresh(w_producers);
  wnoutrefresh(w_validators);
  wnoutrefresh(w_updaters);
  wnoutrefresh(w_queue);
  wnoutrefresh(w_events);
  wnoutrefresh(w_history);
  wnoutrefresh(w_footer);
  doupdate();

  wattron(w_history_in, COLOR_PAIR(CP_SYSTEM) | A_BOLD | A_UNDERLINE);
  mvwprintw(w_history_in, 0, 0, "%-6s %-12s %10s", "TXN#", "TYPE", "AMOUNT");
  wattroff(w_history_in, COLOR_PAIR(CP_SYSTEM) | A_BOLD | A_UNDERLINE);

  touchwin(w_history);
  wnoutrefresh(w_history_in);
  doupdate();

  wbkgd(w_footer, COLOR_PAIR(CP_FOOTER));
  wattron(w_footer, COLOR_PAIR(CP_FOOTER) | A_BOLD);
  mvwprintw(w_footer, 0, 2,
            "Ctrl+C: Stop  |  M: Manual Transaction  |  V=Validators  U=Updaters");
  wattroff(w_footer, COLOR_PAIR(CP_FOOTER) | A_BOLD);
  wrefresh(w_footer);

  refresh();
}

void ui_shutdown() {
  if (w_header) {
      wbkgd(w_header, COLOR_PAIR(CP_ERROR));
      werase(w_header);
      wattron(w_header, COLOR_PAIR(CP_ERROR) | A_BOLD | A_BLINK);
      mvwprintw(w_header, 0, (S_COLS/2)-15, "!!! SYSTEM GRACEFUL SHUTDOWN !!!");
      wattroff(w_header, COLOR_PAIR(CP_ERROR) | A_BOLD | A_BLINK);
      std::this_thread::sleep_for(std::chrono::milliseconds(300 + (rand() % 400)));
  }

  if (w_header) delwin(w_header);
  if (w_producers) delwin(w_producers);
  if (w_validators_in) delwin(w_validators_in);
  if (w_validators) delwin(w_validators);
  if (w_updaters_in) delwin(w_updaters_in);
  if (w_updaters) delwin(w_updaters);
  if (w_events_in) delwin(w_events_in);
  if (w_events) delwin(w_events);
  if (w_history_in) delwin(w_history_in);
  if (w_history) delwin(w_history);
  if (w_footer) delwin(w_footer);
  if (w_wizard) delwin(w_wizard);
  endwin();
}

void ui_update_header(bool auto_mode, bool manual_mode) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  if (!w_header)
    return;
  wbkgd(w_header, COLOR_PAIR(CP_HEADER));
  werase(w_header);
  wattron(w_header, COLOR_PAIR(CP_HEADER) | A_BOLD);

  mvwprintw(w_header, 0, 1, " TRANSACTION PROCESSING SYSTEM ");
  
  std::string mode_str = "[ ";
  if (auto_mode) mode_str += "AUTO ";
  if (manual_mode) mode_str += "MANUAL [M] ";
  mode_str += "]";
  mvwprintw(w_header, 0, 32, "%s", mode_str.c_str());
  wattroff(w_header, COLOR_PAIR(CP_HEADER) | A_BOLD);
  wrefresh(w_header);

  time_t now = time(nullptr);
  char ts[16];
  strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
  mvwprintw(w_header, 0, S_COLS - 10, "%s", ts);

  wattroff(w_header, COLOR_PAIR(CP_HEADER) | A_BOLD);
  wrefresh(w_header);
}

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
      wnoutrefresh(w_validators);
      wnoutrefresh(w_validators_in);
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
      wnoutrefresh(w_updaters);
      wnoutrefresh(w_updaters_in);
      doupdate();
    }
  }
}

void ui_queue_push(int txn_id, const char *user_name, const char *type,
                   double amount) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  QEntry &e = g_qbuf[g_qcount % 32];
  e.id = txn_id;
  snprintf(e.user, sizeof(e.user), "%s", user_name);
  snprintf(e.type, sizeof(e.type), "%s", type);
  e.amount = amount;
  g_qcount++;
  g_current_buf_count++;
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

void ui_add_log(const char *thread_type, int thread_num, const char *message,
                const char *) {
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

  wattron(w_events_in, COLOR_PAIR(cp) | A_BOLD);
  if (thread_num > 0)
    wprintw(w_events_in, "%s-%d: ", thread_type, thread_num);
  else
    wprintw(w_events_in, "%s: ", thread_type);
  wattroff(w_events_in, COLOR_PAIR(cp) | A_BOLD);

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
  refresh_with_wizard(w_events, w_events_in);
}

void ui_update_monitor(int, int, int, int, int, int, int, int, double, int, int, int) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  redraw_queue();
  if (w_header) {
      time_t now = time(nullptr);
      char ts[16];
      strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
      mvwprintw(w_header, 0, S_COLS - 10, "%s", ts);
      wrefresh(w_header);
  }
}

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
  refresh_with_wizard(w_history, w_history_in);
}

// Ensures the wizard window is allocated; internal UI utility.
static void ensure_wizard() {
  if (!w_wizard) {
    w_wizard = newwin(10, S_COLS / 2, HEADER_H + 2, S_COLS / 4);
    wbkgd(w_wizard, COLOR_PAIR(CP_WIZARD));
  }
}

void ui_wizard_clear() {
    std::lock_guard<std::mutex> lk(g_ui_mutex);
    ensure_wizard();
    werase(w_wizard);
    titled_box(w_wizard, "MANUAL TRANSACTION WIZARD", CP_HEADER);
    wrefresh(w_wizard);
}

void ui_wizard_shutdown() {
    std::lock_guard<std::mutex> lk(g_ui_mutex);
    if (w_wizard) {
        delwin(w_wizard);
        w_wizard = nullptr;
    }
    
    clearok(stdscr, TRUE);
    touchwin(stdscr);
    
    if (w_header) { touchwin(w_header); wnoutrefresh(w_header); }
    if (w_producers) { touchwin(w_producers); wnoutrefresh(w_producers); }
    if (w_validators) { touchwin(w_validators); wnoutrefresh(w_validators); }
    if (w_updaters) { touchwin(w_updaters); wnoutrefresh(w_updaters); }
    if (w_queue) { touchwin(w_queue); wnoutrefresh(w_queue); }
    if (w_events) { touchwin(w_events); wnoutrefresh(w_events); }
    if (w_history) { touchwin(w_history); wnoutrefresh(w_history); }
    if (w_footer) { touchwin(w_footer); wnoutrefresh(w_footer); }
    
    doupdate();
    clearok(stdscr, FALSE);
}

void ui_wizard_print(int row, int col, const char *text, int color_pair) {
  std::lock_guard<std::mutex> lk(g_ui_mutex);
  ensure_wizard();
  
  if (color_pair == CP_SUCCESS) init_pair(20, COLOR_GREEN, COLOR_BLUE);
  else if (color_pair == CP_ERROR) init_pair(21, COLOR_RED, COLOR_BLUE);
  else if (color_pair == CP_PRODUCER) init_pair(22, COLOR_CYAN, COLOR_BLUE);
  
  int actual_cp = CP_WIZARD;
  if (color_pair == CP_SUCCESS) actual_cp = 20;
  else if (color_pair == CP_ERROR) actual_cp = 21;
  else if (color_pair == CP_PRODUCER) actual_cp = 22;

  wattron(w_wizard, COLOR_PAIR(actual_cp) | A_BOLD);
  mvwprintw(w_wizard, row + 1, col + 2, "%s", text);
  wattroff(w_wizard, COLOR_PAIR(actual_cp) | A_BOLD);
  wrefresh(w_wizard);
}

bool ui_wizard_get_string(char *buf, int max_len, const char *prompt) {
  extern volatile sig_atomic_t g_running;

  std::lock_guard<std::mutex> lk(g_ui_mutex);
  ensure_wizard();
  
  wattron(w_wizard, COLOR_PAIR(CP_WIZARD) | A_BOLD);
  mvwprintw(w_wizard, 8, 2, "%-36s", prompt);
  wattroff(w_wizard, COLOR_PAIR(CP_WIZARD) | A_BOLD);
  wrefresh(w_wizard);

  echo();
  curs_set(1);
  nodelay(w_wizard, TRUE);
  
  int idx = 0;
  buf[0] = '\0';

  while (true) {
    if (!g_running) break;
    int ch = wgetch(w_wizard);
    if (ch == ERR) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
    }
    if (ch == '\n' || ch == '\r') {
        buf[idx] = '\0';
        break;
    }
    if (ch == 127 || ch == KEY_BACKSPACE || ch == '\b') {
        if (idx > 0) {
            idx--;
            mvwaddch(w_wizard, 8, 2 + strlen(prompt) + idx, ' ');
            wmove(w_wizard, 8, 2 + strlen(prompt) + idx);
        }
    } else if (idx < max_len - 1) {
        buf[idx++] = (char)ch;
    }
    wrefresh(w_wizard);
  }

  nodelay(w_wizard, FALSE);
  noecho();
  curs_set(0);
  return (bool)g_running;
}

void ui_show_final_report(int generated, int done, int rejected, int pending,
                          int processing, int forwarded, int failed,
                          int committed) {
  printf("\n============================================================\n");
  printf("  FINAL REPORT\n");
  printf("============================================================\n");
  printf("  Generated  : %d\n", generated);
  printf("  Committed  : %d\n", committed);
  printf("  Done       : %d\n", done);
  printf("  Rejected   : %d\n", rejected);
  printf("  Pending    : %d\n", pending);
  printf("  Processing : %d\n", processing);
  printf("  Forwarded  : %d\n", forwarded);
  printf("  Failed     : %d\n", failed);
  printf("============================================================\n\n");
}