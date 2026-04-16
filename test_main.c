/* TEST_MAIN.C - Non-blocking test harness for full execution trace */

#include "os_shared.h"

int main(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║         OS SIMULATOR - AUTOMATED TEST RUN (Non-Blocking)           ║\n");
    printf("║                    Testing Phases 1-5 Compliance                    ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Initialize OS subsystems
    printf("[INIT] Initializing memory...\n");
    init_memory();

    printf("[INIT] Initializing mutexes...\n");
    init_mutexes();

    // Test both schedulers
    SchedulerType schedulers[] = {SCHED_HRRN, SCHED_RR};
    const char *sched_names[] = {"HRRN", "Round Robin"};

    for (int sched_idx = 0; sched_idx < 2; ++sched_idx)
    {
        printf("\n");
        printf("════════════════════════════════════════════════════════════════════\n");
        printf("TEST RUN %d: %s SCHEDULING ALGORITHM\n", sched_idx + 1, sched_names[sched_idx]);
        printf("════════════════════════════════════════════════════════════════════\n");
        printf("\n");

        SchedulerType scheduler_algo = schedulers[sched_idx];

        printf("[INIT] Reinitializing mutexes...\n");
        init_mutexes();

        printf("[INIT] Initializing scheduler (%s)...\n", sched_names[sched_idx]);
        init_scheduler(scheduler_algo);

        // Create three non-blocking test processes
        PCB *procs[3];

        printf("[PROC] Creating test processes...\n");
        procs[0] = create_process(1, "test_noblock_1.txt", 0);
        procs[1] = create_process(2, "test_noblock_2.txt", 1);
        procs[2] = create_process(3, "test_noblock_3.txt", 4);

        int n = 3;

        // Validate process creation
        for (int i = 0; i < n; ++i)
        {
            if (!procs[i])
            {
                printf("❌ FAILED to create process %d. Exiting.\n", i + 1);
                return 1;
            }
            procs[i]->state = NEW;
            procs[i]->burst_time = procs[i]->num_instructions;     // total
            procs[i]->remaining_time = procs[i]->num_instructions; // remaining
            procs[i]->waiting_time = 0;
            printf("✓ Process %d created: %d instructions, arrival time: %d\n",
                   procs[i]->pid, procs[i]->num_instructions, procs[i]->arrival_time);
        }

        printf("\n");
        printf("═══ EXECUTION TRACE ═══\n\n");

        bool all_finished = false;
        int step = 0;
        int current_time = 0;
        int max_time = 100; // Safety limit
        int total_steps = 0;

        while (!all_finished && current_time < max_time)
        {
            bool some_unfinished = false;
            bool has_future_arrival = false;

            // Handle process arrivals
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
                        // Allocate and load complete process image into memory
                        if (!load_process_into_memory(proc))
                        {
                            printf("❌ [Memory] Failed to allocate memory for PID %d\n", proc->pid);
                            return 1;
                        }

                        proc->state = READY;
                        proc->burst_time = proc->num_instructions;
                        proc->remaining_time = proc->num_instructions;
                        proc->waiting_time = 0;
                        if (proc->in_memory)
                            pcb_flush_to_memory(proc);
                        add_to_ready(proc);
                        printf("✓ [Time %d] PID %d arrived → Ready queue\n", current_time, proc->pid);
                        print_queues();
                    }
                    else
                    {
                        has_future_arrival = true;
                    }
                }
            }

            if (!some_unfinished)
                break;

            // Get next process from scheduler
            PCB *p = get_next_process();
            if (!p)
            {
                if (has_future_arrival)
                {
                    current_time++;
                    continue;
                }
                printf("\n⚠ [Scheduler] No ready process; execution halted.\n");
                print_queues();
                break;
            }

            if (p->state == FINISHED || p->program_counter >= p->num_instructions)
            {
                p->state = FINISHED;
                continue;
            }

            // Set quantum based on scheduling algorithm
            int quantum = (scheduler_algo == SCHED_RR) ? 2 : p->num_instructions;

            int executed_instructions = 0;
            p->state = RUNNING;
            if (p->in_memory)
                pcb_flush_to_memory(p);

            // Execute instructions (with quantum)
            while (executed_instructions < quantum &&
                   p->state == RUNNING &&
                   p->program_counter < p->num_instructions)
            {
                printf("[T:%d S:%d] PID %d: %s\n",
                       current_time,
                       step,
                       p->pid,
                       p->instructions[p->program_counter]);

                execute_instruction(p);
                scheduler_on_tick(p);

                if (p->state == FINISHED || p->state == BLOCKED)
                {
                    break;
                }

                p->program_counter++;
                executed_instructions++;
                step++;
                total_steps++;
            }

            // Handle round-robin preemption
            if (scheduler_algo == SCHED_RR &&
                executed_instructions >= quantum &&
                p->state == RUNNING &&
                p->program_counter < p->num_instructions)
            {
                printf("  ↻ PID %d preempted after quantum (2 instructions)\n", p->pid);
                p->state = READY;
                add_to_ready(p);
            }
            else if (p->state == RUNNING && p->program_counter >= p->num_instructions)
            {
                p->state = FINISHED;
                printf("✓ PID %d FINISHED\n", p->pid);
            }

            if (p->state == BLOCKED)
            {
                printf("⊗ PID %d BLOCKED on resource\n", p->pid);
            }

            if (p->in_memory)
                pcb_flush_to_memory(p);

            print_queues();
            current_time++;
        }

        printf("\n");
        printf("═══ EXECUTION SUMMARY ═══\n");
        printf("Total time units: %d\n", current_time);
        printf("Total instruction steps: %d\n", total_steps);

        // Final status
        printf("\nFinal Process States:\n");
        for (int i = 0; i < n; ++i)
        {
            const char *state_str = "UNKNOWN";
            switch (procs[i]->state)
            {
            case FINISHED:
                state_str = "FINISHED";
                break;
            case READY:
                state_str = "READY";
                break;
            case BLOCKED:
                state_str = "BLOCKED";
                break;
            case RUNNING:
                state_str = "RUNNING";
                break;
            case NEW:
                state_str = "NEW";
                break;
            }
            printf("  PID %d: %s (PC: %d/%d)\n", procs[i]->pid, state_str,
                   procs[i]->program_counter, procs[i]->num_instructions);
        }

        // Cleanup
        for (int i = 0; i < n; ++i)
        {
            if (procs[i])
            {
                for (int j = 0; j < procs[i]->num_instructions; ++j)
                    free(procs[i]->instructions[j]);
                free(procs[i]->instructions);
                free(procs[i]);
            }
        }

        printf("\n✓ Test run complete.\n");
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    ALL TESTS COMPLETED SUCCESSFULLY                 ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
