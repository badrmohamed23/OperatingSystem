#include "os_shared.h"

int main(void)
{
    init_mutexes();

    // Initialize scheduler with HRRN (can switch to SCHED_RR if desired)
    init_scheduler(SCHED_HRRN);

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
        procs[i]->state = READY;
        // Initialize burst_time as remaining instructions for HRRN
        procs[i]->burst_time = procs[i]->num_instructions;
        procs[i]->waiting_time = 0;
        add_to_ready(procs[i]);
    }

    printf("\n=== Starting scheduler-driven interpreter (Phase 4) ===\n");

    bool all_finished = false;
    int step = 0;

    while (!all_finished)
    {
        // Check if all processes finished
        all_finished = true;
        for (int i = 0; i < n; ++i)
        {
            if (procs[i] && procs[i]->state != FINISHED)
            {
                all_finished = false;
                break;
            }
        }
        if (all_finished)
            break;

        PCB *p = get_next_process();
        if (!p)
        {
            // No ready process but some are not finished -> deadlock or all blocked
            printf("\n[Scheduler] No ready process; all remaining processes are blocked.\n");
            print_queues();
            break;
        }

        if (p->state == FINISHED || p->program_counter >= p->num_instructions)
        {
            p->state = FINISHED;
            continue;
        }

        printf("\n[Step %d] PID %d executing instruction %d: %s\n",
               step,
               p->pid,
               p->program_counter,
               p->instructions[p->program_counter]);

        p->state = RUNNING;
        execute_instruction(p);

        // One instruction of CPU time has passed
        scheduler_on_tick(p);

        if (p->state == FINISHED || p->program_counter >= p->num_instructions || p->burst_time <= 0)
        {
            p->state = FINISHED;
        }
        else if (p->state == BLOCKED)
        {
            // Already moved to blocked queue by sem_wait
        }
        else
        {
            p->state = READY;
            add_to_ready(p);
        }

        ++step;
    }

    printf("\n=== All processes finished or no ready processes (Phase 4). ===\n");

    return 0;
}
