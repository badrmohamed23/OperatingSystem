#include "os_shared.h"

// Global memory and allocation table definitions
MemoryWord memory[MEMORY_SIZE];
int mem_owner[MEMORY_SIZE]; // -1 = free, otherwise PID

// Swap storage for swapped-out processes
static MemoryWord swap_space[MEMORY_SIZE];
static int swap_owner[MEMORY_SIZE]; // -1 = free, otherwise PID

// For now, each process gets a fixed block of N words.
// The first PCB_WORDS within that block are reserved for PCB metadata
// (pid, state, PC, bounds). The remaining words are available for
// variables.
#define WORDS_PER_PROCESS 10

static int find_free_block(int *owner_array, int size)
{
    for (int i = 0; i <= size - WORDS_PER_PROCESS; ++i)
    {
        bool all_free = true;
        for (int j = 0; j < WORDS_PER_PROCESS; ++j)
        {
            if (owner_array[i + j] != -1)
            {
                all_free = false;
                break;
            }
        }
        if (all_free)
            return i;
    }
    return -1;
}

void init_memory(void)
{
    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        memory[i].name[0] = '\0';
        memory[i].value[0] = '\0';
        mem_owner[i] = -1;
        swap_space[i].name[0] = '\0';
        swap_space[i].value[0] = '\0';
        swap_owner[i] = -1;
    }
}

// ---------------------------------------------------------------------
// PCB <-> memory synchronization
// ---------------------------------------------------------------------

// Serialize core PCB fields into the first PCB_WORDS of the
// process's allocated block. Layout (relative to p->mem_start):
//   0: PID
//   1: STATE (as integer enum)
//   2: PC (program counter)
//   3: BOUNDS ("start-end" string for debugging)
void pcb_flush_to_memory(PCB *p)
{
    if (!p)
        return;

    if (!p->in_memory)
        return;

    if (p->mem_start < 0 || p->mem_start + PCB_WORDS > MEMORY_SIZE)
        return;

    int base = p->mem_start;

    char buf[64];

    // Word 0: PID
    strncpy(memory[base + 0].name, "PID", MAX_VAR_NAME - 1);
    memory[base + 0].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d", p->pid);
    strncpy(memory[base + 0].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 0].value[MAX_VAR_VALUE - 1] = '\0';

    // Word 1: STATE
    strncpy(memory[base + 1].name, "STATE", MAX_VAR_NAME - 1);
    memory[base + 1].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d", (int)p->state);
    strncpy(memory[base + 1].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 1].value[MAX_VAR_VALUE - 1] = '\0';

    // Word 2: PC
    strncpy(memory[base + 2].name, "PC", MAX_VAR_NAME - 1);
    memory[base + 2].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d", p->program_counter);
    strncpy(memory[base + 2].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 2].value[MAX_VAR_VALUE - 1] = '\0';

    // Word 3: BOUNDS ("start-end")
    strncpy(memory[base + 3].name, "BOUNDS", MAX_VAR_NAME - 1);
    memory[base + 3].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d-%d", p->mem_start, p->mem_end);
    strncpy(memory[base + 3].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 3].value[MAX_VAR_VALUE - 1] = '\0';
}

// First-fit allocation for WORDS_PER_PROCESS consecutive words
bool allocate_memory_block(PCB *p)
{
    if (p == NULL)
        return false;

    int needed = WORDS_PER_PROCESS;

    for (int i = 0; i <= MEMORY_SIZE - needed; ++i)
    {
        bool all_free = true;
        for (int j = 0; j < needed; ++j)
        {
            if (mem_owner[i + j] != -1)
            {
                all_free = false;
                break;
            }
        }

        if (all_free)
        {
            for (int j = 0; j < needed; ++j)
            {
                mem_owner[i + j] = p->pid;
                memory[i + j].name[0] = '\0';
                memory[i + j].value[0] = '\0';
            }
            p->mem_start = i;
            p->mem_end = i + needed - 1;
            p->in_memory = true;
            // Initialize PCB words inside this block
            pcb_flush_to_memory(p);
            return true;
        }
    }

    return false; // no suitable block
}

bool swap_out(PCB *p)
{
    if (!p || !p->in_memory)
        return false;

    int swap_start = find_free_block(swap_owner, MEMORY_SIZE);
    if (swap_start < 0)
        return false;

    for (int j = 0; j < WORDS_PER_PROCESS; ++j)
    {
        int src = p->mem_start + j;
        int dst = swap_start + j;
        swap_owner[dst] = p->pid;
        strcpy(swap_space[dst].name, memory[src].name);
        strcpy(swap_space[dst].value, memory[src].value);
    }

    int old_start = p->mem_start;
    int old_end = p->mem_end;
    free_memory_block(p);
    p->swap_start = swap_start;
    p->in_memory = false;

    printf("[Memory] Swapped out PID %d from mem[%d..%d] to swap[%d..%d]\n",
           p->pid, old_start, old_end, swap_start, swap_start + WORDS_PER_PROCESS - 1);
    return true;
}

bool swap_in(PCB *p)
{
    if (!p || p->in_memory)
        return true;

    if (p->swap_start < 0)
        return false;

    if (!allocate_memory_block(p))
        return false;

    for (int j = 0; j < WORDS_PER_PROCESS; ++j)
    {
        int src = p->swap_start + j;
        int dst = p->mem_start + j;
        strcpy(memory[dst].name, swap_space[src].name);
        strcpy(memory[dst].value, swap_space[src].value);
        swap_owner[src] = -1;
        swap_space[src].name[0] = '\0';
        swap_space[src].value[0] = '\0';
    }

    int restored_start = p->mem_start;
    int restored_end = p->mem_end;
    p->swap_start = -1;
    p->in_memory = true;

    // Update PCB metadata (especially bounds) to reflect the
    // new in-memory location.
    pcb_flush_to_memory(p);

    printf("[Memory] Swapped in PID %d to mem[%d..%d]\n", p->pid, restored_start, restored_end);
    return true;
}

void free_memory_block(PCB *p)
{
    if (p == NULL)
        return;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return;

    for (int i = p->mem_start; i <= p->mem_end; ++i)
    {
        if (mem_owner[i] == p->pid)
        {
            mem_owner[i] = -1;
        }
        memory[i].name[0] = '\0';
        memory[i].value[0] = '\0';
    }

    p->mem_start = -1;
    p->mem_end = -1;
    p->in_memory = false;
}

bool store_variable(PCB *p, char *var_name, char *value)
{
    if (p == NULL || var_name == NULL || value == NULL)
        return false;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return false;

    // First pass: check if variable already exists in this process's block
    // Skip the PCB metadata words at the start of the block.
    for (int i = p->mem_start + PCB_WORDS; i <= p->mem_end; ++i)
    {
        if (mem_owner[i] != p->pid)
            continue;

        if (memory[i].name[0] != '\0' && strcmp(memory[i].name, var_name) == 0)
        {
            // Overwrite value (and name to be safe)
            strncpy(memory[i].name, var_name, MAX_VAR_NAME - 1);
            memory[i].name[MAX_VAR_NAME - 1] = '\0';

            strncpy(memory[i].value, value, MAX_VAR_VALUE - 1);
            memory[i].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    // Second pass: look for an empty slot in this process's block
    // Again skip PCB metadata words.
    for (int i = p->mem_start + PCB_WORDS; i <= p->mem_end; ++i)
    {
        if (mem_owner[i] != p->pid)
            continue;

        if (memory[i].name[0] == '\0')
        {
            strncpy(memory[i].name, var_name, MAX_VAR_NAME - 1);
            memory[i].name[MAX_VAR_NAME - 1] = '\0';

            strncpy(memory[i].value, value, MAX_VAR_VALUE - 1);
            memory[i].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    // No space left in this process's block
    return false;
}

char *load_variable(PCB *p, char *var_name)
{
    if (p == NULL || var_name == NULL)
        return NULL;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return NULL;

    // Skip PCB metadata words when searching for variables.
    for (int i = p->mem_start + PCB_WORDS; i <= p->mem_end; ++i)
    {
        if (mem_owner[i] != p->pid)
            continue;

        if (memory[i].name[0] != '\0' && strcmp(memory[i].name, var_name) == 0)
        {
            return memory[i].value;
        }
    }

    return NULL; // not found
}

void print_memory(void)
{
    printf("==================== MEMORY DUMP ====================\n");
    printf("Idx | Owner | Name                           | Value\n");
    printf("------------------------------------------------------\n");

    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        const char *name = (memory[i].name[0] != '\0') ? memory[i].name : "-";
        const char *value = (memory[i].value[0] != '\0') ? memory[i].value : "-";
        printf("%3d | %5d | %-30s | %s\n", i, mem_owner[i], name, value);
    }

    printf("======================================================\n");
}

void print_swap_space(void)
{
    printf("==================== SWAP SPACE DUMP =================\n");
    printf("Idx | Owner | Name                           | Value\n");
    printf("------------------------------------------------------\n");

    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        const char *name = (swap_space[i].name[0] != '\0') ? swap_space[i].name : "-";
        const char *value = (swap_space[i].value[0] != '\0') ? swap_space[i].value : "-";
        printf("%3d | %5d | %-30s | %s\n", i, swap_owner[i], name, value);
    }

    printf("======================================================\n");
}

// Dummy main to test memory functions standalone
#ifdef TEST_MEMORY_MAIN
int main(void)
{
    PCB p1;
    p1.pid = 1;
    p1.mem_start = -1;
    p1.mem_end = -1;

    init_memory();

    if (!allocate_memory_block(&p1))
    {
        printf("Failed to allocate memory block for process %d\n", p1.pid);
        return 1;
    }

    printf("Allocated block for PID %d: [%d, %d]\n", p1.pid, p1.mem_start, p1.mem_end);

    store_variable(&p1, "x", "10");
    store_variable(&p1, "y", "20");
    store_variable(&p1, "msg", "hello");

    // Overwrite existing variable
    store_variable(&p1, "x", "42");

    char *val_x = load_variable(&p1, "x");
    char *val_y = load_variable(&p1, "y");
    char *val_msg = load_variable(&p1, "msg");
    char *val_z = load_variable(&p1, "z"); // should be NULL

    printf("Loaded x = %s\n", val_x ? val_x : "(null)");
    printf("Loaded y = %s\n", val_y ? val_y : "(null)");
    printf("Loaded msg = %s\n", val_msg ? val_msg : "(null)");
    printf("Loaded z = %s\n", val_z ? val_z : "(null)");

    print_memory();

    free_memory_block(&p1);

    printf("After freeing block for PID %d:\n", p1.pid);
    print_memory();

    return 0;
}
#endif
