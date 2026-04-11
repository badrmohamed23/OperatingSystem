#include "os_shared.h"

// Global memory and allocation table definitions
MemoryWord memory[MEMORY_SIZE];
int mem_owner[MEMORY_SIZE]; // -1 = free, otherwise PID

// For now, each process gets a fixed block of 3 words
#define WORDS_PER_PROCESS 3

void init_memory(void)
{
    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        memory[i].name[0] = '\0';
        memory[i].value[0] = '\0';
        mem_owner[i] = -1;
    }
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
            return true;
        }
    }

    return false; // no suitable block
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
}

bool store_variable(PCB *p, char *var_name, char *value)
{
    if (p == NULL || var_name == NULL || value == NULL)
        return false;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return false;

    // First pass: check if variable already exists in this process's block
    for (int i = p->mem_start; i <= p->mem_end; ++i)
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
    for (int i = p->mem_start; i <= p->mem_end; ++i)
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

    for (int i = p->mem_start; i <= p->mem_end; ++i)
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
