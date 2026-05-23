#include "os_shared.h"

// Global simulated memory
MemoryWord memory[MEMORY_SIZE];
int mem_owner[MEMORY_SIZE]; // -1 = free

void init_memory(void)
{
    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        mem_owner[i] = -1;
        memory[i].name[0] = '\0';
        memory[i].value[0] = '\0';
    }
}

void pcb_flush_to_memory(PCB *p)
{
    if (!p || !p->in_memory || p->mem_start < 0)
        return;

    int base = p->mem_start;
    // word 0: PID
    snprintf(memory[base + 0].name, MAX_VAR_NAME, "PID");
    snprintf(memory[base + 0].value, MAX_VAR_VALUE, "%d", p->pid);
    // word 1: STATE
    snprintf(memory[base + 1].name, MAX_VAR_NAME, "STATE");
    snprintf(memory[base + 1].value, MAX_VAR_VALUE, "%d", (int)p->state);
    // word 2: PC
    snprintf(memory[base + 2].name, MAX_VAR_NAME, "PC");
    snprintf(memory[base + 2].value, MAX_VAR_VALUE, "%d", p->program_counter);
    // word 3: BOUNDS
    snprintf(memory[base + 3].name, MAX_VAR_NAME, "BOUNDS");
    snprintf(memory[base + 3].value, MAX_VAR_VALUE, "%d-%d", p->mem_start, p->mem_end);
}

bool allocate_memory_block(PCB *p)
{
    if (!p)
        return false;

    int required = PCB_WORDS + PROCESS_VAR_SLOTS + p->num_instructions;
    if (required > MEMORY_SIZE)
        return false;

    for (int i = 0; i + required <= MEMORY_SIZE; ++i)
    {
        bool ok = true;
        for (int j = 0; j < required; ++j)
        {
            if (mem_owner[i + j] != -1)
            {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;

        // allocate
        for (int j = 0; j < required; ++j)
        {
            mem_owner[i + j] = p->pid;
            memory[i + j].name[0] = '\0';
            memory[i + j].value[0] = '\0';
        }

        p->mem_start = i;
        p->mem_end = i + required - 1;
        p->in_memory = true;

        pcb_flush_to_memory(p);
        return true;
    }

    return false;
}

void free_memory_block(PCB *p)
{
    if (!p || p->mem_start < 0)
        return;

    for (int i = p->mem_start; i <= p->mem_end && i < MEMORY_SIZE; ++i)
    {
        mem_owner[i] = -1;
        memory[i].name[0] = '\0';
        memory[i].value[0] = '\0';
    }
    p->mem_start = -1;
    p->mem_end = -1;
    p->in_memory = false;
}

bool store_variable(PCB *p, char *var_name, char *value)
{
    if (!p || !var_name || !value || !p->in_memory)
        return false;

    int base = p->mem_start + PCB_WORDS;
    for (int i = 0; i < PROCESS_VAR_SLOTS; ++i)
    {
        int idx = base + i;
        if (strncmp(memory[idx].name, var_name, MAX_VAR_NAME) == 0)
        {
            strncpy(memory[idx].value, value, MAX_VAR_VALUE - 1);
            memory[idx].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    // find empty slot
    for (int i = 0; i < PROCESS_VAR_SLOTS; ++i)
    {
        int idx = base + i;
        if (memory[idx].name[0] == '\0')
        {
            strncpy(memory[idx].name, var_name, MAX_VAR_NAME - 1);
            memory[idx].name[MAX_VAR_NAME - 1] = '\0';
            strncpy(memory[idx].value, value, MAX_VAR_VALUE - 1);
            memory[idx].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    // no space
    return false;
}

char *load_variable(PCB *p, char *var_name)
{
    if (!p || !var_name || !p->in_memory)
        return NULL;

    int base = p->mem_start + PCB_WORDS;
    for (int i = 0; i < PROCESS_VAR_SLOTS; ++i)
    {
        int idx = base + i;
        if (strncmp(memory[idx].name, var_name, MAX_VAR_NAME) == 0)
            return memory[idx].value;
    }
    return NULL;
}

bool load_process_into_memory(PCB *p)
{
    if (!p)
        return false;
    if (p->in_memory)
        return true;

    if (!allocate_memory_block(p))
        return false;

    // write PCB metadata and variables (variables empty initially)
    pcb_flush_to_memory(p);

    // write instructions
    int instr_base = p->mem_start + PCB_WORDS + PROCESS_VAR_SLOTS;
    for (int i = 0; i < p->num_instructions; ++i)
    {
        int idx = instr_base + i;
        strncpy(memory[idx].value, p->instructions[i], MAX_VAR_VALUE - 1);
        memory[idx].value[MAX_VAR_VALUE - 1] = '\0';
    }

    return true;
}

bool load_instruction(PCB *p, int pc, char *out_buffer, int max_size)
{
    if (!p || !p->in_memory || pc < 0 || pc >= p->num_instructions)
        return false;

    int idx = p->mem_start + PCB_WORDS + PROCESS_VAR_SLOTS + pc;
    strncpy(out_buffer, memory[idx].value, max_size - 1);
    out_buffer[max_size - 1] = '\0';
    return true;
}

void print_memory(void)
{
    printf("-- Memory (%d words) --\n", MEMORY_SIZE);
    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        printf("[%02d] owner=%3d name=%-8s value=%s\n", i, mem_owner[i], memory[i].name[0] ? memory[i].name : "-", memory[i].value[0] ? memory[i].value : "-");
    }
}

void print_swap_space(void)
{
    printf("-- Swap space: not implemented (swap_out uses logical unload) --\n");
}

bool swap_out(PCB *p)
{
    if (!p || !p->in_memory)
        return false;

    // Simple unload: free memory block but keep instructions in heap
    free_memory_block(p);
    // For simplicity we don't persist to disk here
    return true;
}

bool swap_in(PCB *p)
{
    if (!p || p->in_memory)
        return false;

    // Try to allocate and copy instructions back into memory
    return load_process_into_memory(p);
}

