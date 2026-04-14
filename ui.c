#include "os_shared.h"

// pdcurses defines its own bool; avoid conflict with <stdbool.h>
#undef bool
#define HAVE_NO_INFOEX 1
#include "curses.h"

#include <stdarg.h>

static int ui_enabled = 0;

void ui_init(int enable)
{
    if (enable)
    {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        ui_enabled = 1;
    }
    else
    {
        ui_enabled = 0;
    }
}

void ui_shutdown(void)
{
    if (ui_enabled)
    {
        endwin();
        ui_enabled = 0;
    }
}

void ui_draw_step(int current_time, SchedulerType algo, PCB *running)
{
    if (!ui_enabled)
        return;

    erase();

    const char *algo_name = "Unknown";
    switch (algo)
    {
    case SCHED_HRRN:
        algo_name = "HRRN";
        break;
    case SCHED_RR:
        algo_name = "Round Robin";
        break;
    case SCHED_MLFQ:
        algo_name = "MLFQ";
        break;
    }

    mvprintw(0, 0, "OS Simulator (curses view)");
    mvprintw(1, 0, "Time: %d", current_time);
    mvprintw(2, 0, "Scheduler: %s", algo_name);

    if (running)
    {
        mvprintw(4, 0, "Running PID: %d", running->pid);
        mvprintw(5, 0, "State: %d  PC: %d/%d", (int)running->state,
                 running->program_counter, running->num_instructions);
        mvprintw(6, 0, "Priority level: %d", running->priority_level);
    }
    else
    {
        mvprintw(4, 0, "Running PID: (none)");
    }

    mvprintw(8, 0, "(Text logs still printed below in terminal)");
    mvprintw(10, 0, "Press Ctrl+C in shell to terminate if needed.");

    refresh();
}
