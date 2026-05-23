#include "os_shared.h"

typedef struct
{
    int pid;
    const char *file;
    int arrival_time;
    PCB *pcb;
} ProcessSpec;

static void discard_stdin_line(void)
{
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF)
    {
    }
}

static PCB *choose_swap_victim(ProcessSpec *specs, int count, int exclude_pid)
{
    for (int i = 0; i < count; ++i)
    {
        PCB *cand = specs[i].pcb;
        if (!cand)
            continue;
        if (cand->pid == exclude_pid)
            continue;
        if (!cand->in_memory)
            continue;
        if (cand->state == FINISHED)
            continue;
        return cand;
    }
    return NULL;
}

static bool ensure_loaded_for_new_process(ProcessSpec *specs, int count, PCB *p)
{
    if (load_process_into_memory(p))
        return true;

    while (1)
    {
        PCB *victim = choose_swap_victim(specs, count, p->pid);
        if (!victim)
            return false;

        if (!swap_out(victim))
            return false;

        if (load_process_into_memory(p))
            return true;
    }
}

static bool ensure_in_memory(ProcessSpec *specs, int count, PCB *p)
{
    if (!p)
        return false;

    if (p->in_memory)
        return true;

    if (swap_in(p))
        return true;

    while (1)
    {
        PCB *victim = choose_swap_victim(specs, count, p->pid);
        if (!victim)
            return false;

        if (!swap_out(victim))
            return false;

        if (swap_in(p))
            return true;
    }
}

int main(void)
{
    init_memory();
    init_mutexes();

    // UI removed for simplified build — run entirely in console mode.

    // Let the user choose the scheduler algorithm at runtime
    printf("Select scheduler: 0=HRRN, 1=RR, 2=MLFQ > ");
    int choice = 0;
    if (scanf("%d", &choice) != 1)
        choice = 0;
    discard_stdin_line();

    SchedulerType scheduler_algo = SCHED_HRRN;
    if (choice == 1)
        scheduler_algo = SCHED_RR;
    else if (choice == 2)
        scheduler_algo = SCHED_MLFQ;

    init_scheduler(scheduler_algo);

    ProcessSpec specs[] = {
        {1, "programs/Program 1.txt", 0, NULL},
        {2, "programs/Program_2.txt", 1, NULL},
        {3, "programs/Program_3.txt", 4, NULL},
    };
    int spec_count = (int)(sizeof(specs) / sizeof(specs[0]));

    printf("\n=== Starting scheduler-driven interpreter  ===\n");

    int step = 0;
    int current_time = 0;

    while (1)
    {
        bool some_unfinished = false;
        bool has_future_arrival = false;

        for (int i = 0; i < spec_count; ++i)
        {
            ProcessSpec *spec = &specs[i];

            if (!spec->pcb)
            {
                if (spec->arrival_time <= current_time)
                {
                    spec->pcb = create_process(spec->pid, spec->file, spec->arrival_time);
                    if (!spec->pcb)
                    {
                        printf("[Error] Failed to create PID %d at arrival time.\n", spec->pid);
                        return 1;
                    }

                    if (!ensure_loaded_for_new_process(specs, spec_count, spec->pcb))
                    {
                        printf("[Memory] Failed to load PID %d into memory on arrival.\n", spec->pid);
                        return 1;
                    }

                    spec->pcb->state = READY;
                    spec->pcb->burst_time = spec->pcb->num_instructions;
                    spec->pcb->remaining_time = spec->pcb->num_instructions;
                    spec->pcb->waiting_time = 0;

                    if (spec->pcb->in_memory)
                        pcb_flush_to_memory(spec->pcb);

                    add_to_ready(spec->pcb);

                    printf("[Time %d] PID %d arrived, created, loaded in memory, and entered ready queue.\n",
                           current_time, spec->pid);
                    print_queues();
                    print_memory();
                    print_swap_space();
                }
                else
                {
                    has_future_arrival = true;
                    some_unfinished = true;
                }
                continue;
            }

            if (spec->pcb->state != FINISHED)
                some_unfinished = true;
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

        if (!ensure_in_memory(specs, spec_count, p))
        {
            printf("[Memory] Failed to bring PID %d into memory when scheduling.\n", p->pid);
            return 1;
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

        // UI removed: no graphical update.

        while (executed_instructions < quantum && p->state == RUNNING && p->program_counter < p->num_instructions)
        {
            char inst[MAX_INSTRUCTION_LEN];
            if (!load_instruction(p, p->program_counter, inst, sizeof(inst)))
            {
                strcpy(inst, "<instruction unavailable>");
            }

            printf("\n[Time %d][Step %d] PID %d executing instruction %d: %s\n",
                   current_time,
                   step,
                   p->pid,
                   p->program_counter,
                   inst);

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
            // Already moved to blocked queue by sem_wait.
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
    print_swap_space();

    // UI removed: nothing to shutdown.
    return 0;
}
