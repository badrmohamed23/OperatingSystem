#include "os_shared.h"

/*
 * Build with -DUSE_CURSES_UI and link PDCurses to enable a full-screen TUI.
 * Without that flag, this file falls back to a lightweight text mode.
 */

static int ui_enabled = 0;

static const char *algo_to_str(SchedulerType algo)
{
    switch (algo)
    {
    case SCHED_HRRN:
        return "HRRN";
    case SCHED_RR:
        return "Round Robin";
    case SCHED_MLFQ:
        return "MLFQ";
    default:
        return "Unknown";
    }
}

static const char *state_to_str(State s)
{
    switch (s)
    {
    case NEW:
        return "NEW";
    case READY:
        return "READY";
    case RUNNING:
        return "RUNNING";
    case BLOCKED:
        return "BLOCKED";
    case FINISHED:
        return "FINISHED";
    default:
        return "?";
    }
}

#ifdef USE_CURSES_UI
// pdcurses defines its own bool; avoid conflict with <stdbool.h>
#undef bool
#define HAVE_NO_INFOEX 1
#include "curses.h"

static WINDOW *ui_win = NULL;

void ui_init(int enable)
{
    if (!enable)
    {
        ui_enabled = 0;
        return;
    }

    ui_win = initscr();
    if (!ui_win)
    {
        ui_enabled = 0;
        return;
    }
    cbreak();
    noecho();
    keypad(ui_win, TRUE);
    nodelay(ui_win, TRUE);
    curs_set(0);

    if (has_colors())
    {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    }

    ui_enabled = 1;
}

void ui_shutdown(void)
{
    if (!ui_enabled)
        return;

    endwin();
    ui_win = NULL;
    ui_enabled = 0;
}

void ui_draw_step(int current_time, SchedulerType algo, PCB *running)
{
    if (!ui_enabled)
        return;

    int rows = getmaxy(ui_win);
    int cols = getmaxx(ui_win);

    werase(ui_win);

    attron(COLOR_PAIR(1) | A_BOLD);
    mvwprintw(ui_win, 0, 2, "OS Simulator Dashboard");
    attroff(COLOR_PAIR(1) | A_BOLD);

    mvwhline(ui_win, 1, 0, ACS_HLINE, cols);

    attron(COLOR_PAIR(2));
    mvwprintw(ui_win, 3, 2, "Clock Time : %d", current_time);
    mvwprintw(ui_win, 4, 2, "Scheduler  : %s", algo_to_str(algo));
    attroff(COLOR_PAIR(2));

    mvwhline(ui_win, 6, 0, ACS_HLINE, cols);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvwprintw(ui_win, 8, 2, "Running Process");
    attroff(COLOR_PAIR(3) | A_BOLD);

    if (running)
    {
        mvwprintw(ui_win, 10, 4, "PID       : %d", running->pid);
        mvwprintw(ui_win, 11, 4, "State     : %s", state_to_str(running->state));
        mvwprintw(ui_win, 12, 4, "PC        : %d / %d", running->program_counter, running->num_instructions);
        mvwprintw(ui_win, 13, 4, "Priority  : %d", running->priority_level);
        mvwprintw(ui_win, 14, 4, "Memory    : [%d .. %d]", running->mem_start, running->mem_end);
    }
    else
    {
        mvwprintw(ui_win, 10, 4, "No process is running.");
    }

    mvwhline(ui_win, rows - 3, 0, ACS_HLINE, cols);
    mvwprintw(ui_win, rows - 2, 2, "Tip: main logs/queues/memory dumps are printed in the console output.");

    wrefresh(ui_win);
}

#else

void ui_init(int enable)
{
    ui_enabled = enable ? 1 : 0;
    if (ui_enabled)
    {
        printf("[UI] Graphical mode unavailable in this build. Running text fallback.\n");
    }
}

void ui_shutdown(void)
{
    if (ui_enabled)
    {
        printf("[UI] Fallback UI shutdown.\n");
        ui_enabled = 0;
    }
}

void ui_draw_step(int current_time, SchedulerType algo, PCB *running)
{
    if (!ui_enabled)
        return;

    printf("[UI] t=%d algo=%s running=%s\n",
           current_time,
           algo_to_str(algo),
           running ? state_to_str(running->state) : "none");
}

#endif
