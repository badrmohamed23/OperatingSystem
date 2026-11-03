/* tests/unit/test_memory.c – Unit tests for memory allocation and swapping */
#include <assert.h>
#include <stdio.h>
#include "os_shared.h"

int main(void)
{
    printf("Running test_memory...\n");

    MemoryWord memory[MEMORY_SIZE];
    int owners[MEMORY_SIZE];

    init_memory(memory, owners);
    for (int i = 0; i < MEMORY_SIZE; ++i) {
        assert(owners[i] == -1);
        assert(memory[i].name[0] == '\0');
    }

    /* Process 1 with 3 instructions requires 4(PCB) + 3(vars) + 3 = 10 words */
    PCB p1;
    memset(&p1, 0, sizeof(p1));
    p1.pid = 1;
    p1.state = READY;
    p1.num_instructions = 3;
    char *p1_inst[] = {"assign x 5", "assign y 10", "print x"};
    p1.instructions = p1_inst;

    bool ok = allocate_memory_block(&p1, memory, owners);
    assert(ok);
    assert(p1.in_memory);
    assert(p1.mem_start == 0);
    assert(p1.mem_end == 9);

    /* Verify allocation in RAM */
    for (int i = 0; i < 10; ++i) assert(owners[i] == 1);
    for (int i = 10; i < MEMORY_SIZE; ++i) assert(owners[i] == -1);

    /* Store and load variable */
    bool stored = store_variable(&p1, memory, "x", "42");
    assert(stored);
    char *loaded = load_variable(&p1, memory, "x");
    assert(loaded != NULL);
    assert(strcmp(loaded, "42") == 0);

    /* Overwrite variable */
    stored = store_variable(&p1, memory, "x", "99");
    assert(stored);
    loaded = load_variable(&p1, memory, "x");
    assert(strcmp(loaded, "99") == 0);

    /* Swap out P1 */
    bool swapped_out = swap_out(&p1, memory, owners);
    assert(swapped_out);
    assert(!p1.in_memory);
    for (int i = 0; i < MEMORY_SIZE; ++i) assert(owners[i] == -1);

    /* Swap in P1: variables must be preserved */
    bool swapped_in = swap_in(&p1, memory, owners);
    assert(swapped_in);
    assert(p1.in_memory);
    loaded = load_variable(&p1, memory, "x");
    assert(loaded != NULL);
    assert(strcmp(loaded, "99") == 0);

    free_memory_block(&p1, memory, owners);
    assert(!p1.in_memory);

    printf("✓ test_memory passed successfully!\n");
    return 0;
}

