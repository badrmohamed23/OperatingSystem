#include "os_shared.h"

int main(void)
{
    init_memory();
    init_mutexes();

    // Optional: enable curses-based TUI
    printf("Use curses TUI? 0=No, 1=Yes > ");
    int use_tui = 0;
    if (scanf("%d", &use_tui) != 1)
        use_tui = 0;
    ui_init(use_tui != 0);

    // Let the user choose the scheduler algorithm at runtime
    printf("Select scheduler: 0=HRRN, 1=RR, 2=MLFQ > ");
    int choice = 0;
    if (scanf("%d", &choice) != 1)
        choice = 0;

    SchedulerType scheduler_algo = SCHED_HRRN;
    if (choice == 1)
        scheduler_algo = SCHED_RR;
    else if (choice == 2)
        scheduler_algo = SCHED_MLFQ;

    init_scheduler(scheduler_algo);

    // Create three processes from the three program files
    PCB *procs[3];

    procs[0] = create_process(1, "Program 1.txt", 0);
    procs[1] = create_process(2, "Program_2.txt", 1);
    procs[2] = create_process(3, "Program_3.txt", 4);

    int n = 3;

    // Make sure all processes were created successfully
    for (int i = 0; i < n; ++i)
    {
        if (!procs[i])
        {
            printf("Failed to create process %d. Exiting.\n", i + 1);
            return 1;
        }
        procs[i]->state = NEW;
        procs[i]->burst_time = procs[i]->num_instructions;     // total
        procs[i]->remaining_time = procs[i]->num_instructions; // remaining
        procs[i]->waiting_time = 0;
    }

    printf("\n=== Starting scheduler-driven interpreter (Phase 5) ===\n");

    bool all_finished = false;
    int step = 0;
    int current_time = 0;

    while (!all_finished)
    {
        bool some_unfinished = false;
        bool has_future_arrival = false;

        for (int i = 0; i < n; ++i)
        {
            PCB *proc = procs[i];
            if (!proc || proc->state == FINISHED)
                continue;

            some_unfinished = true;

            if (proc->state == NEW)
            {
                if (proc->arrival_time <= current_time)
                {
                    if (!allocate_memory_block(proc))
                    {
                        // Try to free space by swapping out an older process
                        for (int j = 0; j < n; ++j)
                        {
                            if (procs[j] && procs[j]->in_memory && procs[j]->state != FINISHED && procs[j]->pid != proc->pid)
                            {
                                if (swap_out(procs[j]))
                                {
                                    break;
                                }
                            }
                        }
                    }

                    if (!allocate_memory_block(proc))
                    {
                        printf("[Memory] Failed to allocate memory for PID %d on arrival.\n", proc->pid);
                        return 1;
                    }

                    proc->state = READY;
                    proc->burst_time = proc->num_instructions;
                    proc->remaining_time = proc->num_instructions;
                    proc->waiting_time = 0;
                    // Mirror basic code metadata into simulated memory
                    char len_buf[16];
                    snprintf(len_buf, sizeof(len_buf), "%d", proc->num_instructions);
                    store_variable(proc, "__prog_name", proc->program_name);
                    store_variable(proc, "__prog_len", len_buf);
                    if (proc->in_memory)
                        pcb_flush_to_memory(proc);
                    add_to_ready(proc);
                    printf("[Time %d] PID %d arrived and entered ready queue.\n", current_time, proc->pid);
                    print_queues();
                    print_memory();
                }
                else
                {
                    has_future_arrival = true;
                }
            }
        }

        if (!some_unfinished)
            break;

        PCB *p = get_next_process();
        if (!p)
        {
            if (has_future_arrival)
            {
                printf("[Time %d] No ready process; advancing time to next arrival.\n", current_time);
                current_time++;
                continue;
            }

            printf("\n[Scheduler] No ready process; all remaining processes are blocked.\n");
            print_queues();
            print_swap_space();
            break;
        }

        if (!p->in_memory)
        {
            if (!swap_in(p))
            {
                printf("[Memory] Failed to swap in PID %d when scheduling.\n", p->pid);
                return 1;
            }
        }

        if (p->state == FINISHED || p->program_counter >= p->num_instructions)
        {
            p->state = FINISHED;
            continue;
        }

        int quantum = 1;
        if (scheduler_algo == SCHED_RR)
        {
            quantum = 2;
        }
        else if (scheduler_algo == SCHED_MLFQ)
        {
            int level = p->priority_level;
            if (level < 0)
                level = 0;
            if (level > 3)
                level = 3;
            quantum = 1 << level; // 2^i for queue level i
        }

        int executed_instructions = 0;
        p->state = RUNNING;
        if (p->in_memory)
            pcb_flush_to_memory(p);

        // Update curses TUI for this scheduling step
        ui_draw_step(current_time, scheduler_algo, p);

        while (executed_instructions < quantum && p->state == RUNNING && p->program_counter < p->num_instructions)
        {
            printf("\n[Time %d][Step %d] PID %d executing instruction %d: %s\n",
                   current_time,
                   step,
                   p->pid,
                   p->program_counter,
                   p->instructions[p->program_counter]);

            execute_instruction(p);
            scheduler_on_tick(p);

            if (p->state == FINISHED || p->state == BLOCKED)
                break;

            executed_instructions++;
        }

        if (scheduler_algo == SCHED_MLFQ &&
            p->state == RUNNING &&
            p->program_counter < p->num_instructions &&
            executed_instructions >= quantum)
        {
            // Time slice fully used: demote to lower-priority queue (up to level 3)
            if (p->priority_level < 3)
                p->priority_level++;
            p->state = READY;
            add_to_ready(p);
        }
        else if (p->state == FINISHED || p->program_counter >= p->num_instructions || p->remaining_time <= 0)
        {
            p->state = FINISHED;
        }
        else if (p->state == BLOCKED)
        {
            // already blocked by sem_wait
        }
        else
        {
            p->state = READY;
            add_to_ready(p);
        }

        if (p->in_memory)
            pcb_flush_to_memory(p);

        print_queues();
        print_memory();
        print_swap_space();

        ++step;
        ++current_time;
    }

    printf("\n=== All processes finished or no ready processes (Phase 5). ===\n");
    print_memory();

    return 0;
}