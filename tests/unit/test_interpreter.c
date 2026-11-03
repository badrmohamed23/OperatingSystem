/* tests/unit/test_interpreter.c – Unit tests for instruction execution */
#include <assert.h>
#include <stdio.h>
#include "os_shared.h"

int main(void)
{
    printf("Running test_interpreter...\n");

    MemoryWord memory[MEMORY_SIZE];
    int owners[MEMORY_SIZE];
    Mutex mutexes[RES_COUNT];

    init_memory(memory, owners);
    init_mutexes(mutexes);

    Scheduler *sched = scheduler_create(SCHED_RR);

    PCB p;
    memset(&p, 0, sizeof(p));
    p.pid = 1;
    p.state = RUNNING;
    p.num_instructions = 3;
    char *inst[] = {
        "assign val 100",
        "print val",
        "assign other val"
    };
    p.instructions = inst;

    bool ok = load_process_into_memory(&p, memory, owners);
    assert(ok);

    ExecContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem = memory;
    ctx.mem_owner = owners;
    ctx.mutexes = mutexes;
    ctx.scheduler = sched;

    /* Execute: assign val 100 */
    execute_instruction(&p, &ctx);
    assert(p.program_counter == 1);
    char *v = load_variable(&p, memory, "val");
    assert(v && strcmp(v, "100") == 0);

    /* Execute: print val */
    execute_instruction(&p, &ctx);
    assert(p.program_counter == 2);

    /* Execute: assign other val (literal val) */
    execute_instruction(&p, &ctx);
    assert(p.program_counter == 3);
    assert(p.state == FINISHED);

    scheduler_destroy(sched);
    free_memory_block(&p, memory, owners);

    printf("✓ test_interpreter passed successfully!\n");
    return 0;
}

