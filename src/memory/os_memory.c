/* src/memory/os_memory.c – Memory management, swapping, variable storage */
#include "os_shared.h"

typedef struct {
    MemoryWord vars[PROCESS_VAR_SLOTS];
} SwapData;

/* --------------------------------------------------------------------
   pcb_flush_to_memory – writes PCB metadata into its reserved words
   -------------------------------------------------------------------- */
void pcb_flush_to_memory(PCB *p, MemoryWord *mem)
{
    if (!p || !p->in_memory || p->mem_start < 0 || !mem)
        return;

    int base = p->mem_start;
    if (base + PCB_WORDS - 1 >= MEMORY_SIZE) return;

    snprintf(mem[base + 0].name,  MAX_VAR_NAME,  "PID");
    snprintf(mem[base + 0].value, MAX_VAR_VALUE, "%d", p->pid);
    snprintf(mem[base + 1].name,  MAX_VAR_NAME,  "STATE");
    snprintf(mem[base + 1].value, MAX_VAR_VALUE, "%d", (int)p->state);
    snprintf(mem[base + 2].name,  MAX_VAR_NAME,  "PC");
    snprintf(mem[base + 2].value, MAX_VAR_VALUE, "%d", p->program_counter);
    snprintf(mem[base + 3].name,  MAX_VAR_NAME,  "BOUNDS");
    snprintf(mem[base + 3].value, MAX_VAR_VALUE, "%d-%d", p->mem_start, p->mem_end);
}

/* --------------------------------------------------------------------
   init_memory – initializes RAM and owner tables
   -------------------------------------------------------------------- */
void init_memory(MemoryWord *mem, int *owners)
{
    if (!mem || !owners) return;
    for (int i = 0; i < MEMORY_SIZE; ++i) {
        owners[i] = -1;
        mem[i].name[0]  = '\0';
        mem[i].value[0] = '\0';
    }
}

/* --------------------------------------------------------------------
   allocate_memory_block – first-fit contiguous allocation
   -------------------------------------------------------------------- */
bool allocate_memory_block(PCB *p, MemoryWord *mem, int *owners)
{
    if (!p || !mem || !owners)
        return false;

    int required = PCB_WORDS + PROCESS_VAR_SLOTS + p->num_instructions;
    if (required > MEMORY_SIZE)
        return false;

    for (int i = 0; i + required <= MEMORY_SIZE; ++i) {
        bool ok = true;
        for (int j = 0; j < required; ++j) {
            if (owners[i + j] != -1) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;

        /* Allocate the contiguous block */
        for (int j = 0; j < required; ++j) {
            owners[i + j] = p->pid;
            mem[i + j].name[0]  = '\0';
            mem[i + j].value[0] = '\0';
        }
        p->mem_start = i;
        p->mem_end   = i + required - 1;
        p->in_memory = true;

        pcb_flush_to_memory(p, mem);
        return true;
    }

    return false;
}

/* --------------------------------------------------------------------
   free_memory_block – marks memory as free
   -------------------------------------------------------------------- */
void free_memory_block(PCB *p, MemoryWord *mem, int *owners)
{
    if (!p || p->mem_start < 0 || !mem || !owners)
        return;

    for (int i = p->mem_start; i <= p->mem_end && i < MEMORY_SIZE; ++i) {
        owners[i] = -1;
        mem[i].name[0]  = '\0';
        mem[i].value[0] = '\0';
    }
    p->mem_start = -1;
    p->mem_end   = -1;
    p->in_memory = false;
}

/* --------------------------------------------------------------------
   store_variable – stores a variable in process's assigned variable slots
   -------------------------------------------------------------------- */
bool store_variable(PCB *p, MemoryWord *mem, const char *var_name, const char *value)
{
    if (!p || !var_name || !value || !p->in_memory || !mem)
        return false;

    int base = p->mem_start + PCB_WORDS;

    /* Overwrite if variable already exists */
    for (int i = 0; i < PROCESS_VAR_SLOTS; ++i) {
        int idx = base + i;
        if (idx >= MEMORY_SIZE) break;
        if (strncmp(mem[idx].name, var_name, MAX_VAR_NAME) == 0) {
            strncpy(mem[idx].value, value, MAX_VAR_VALUE - 1);
            mem[idx].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    /* Assign into first empty slot */
    for (int i = 0; i < PROCESS_VAR_SLOTS; ++i) {
        int idx = base + i;
        if (idx >= MEMORY_SIZE) break;
        if (mem[idx].name[0] == '\0') {
            strncpy(mem[idx].name, var_name, MAX_VAR_NAME - 1);
            mem[idx].name[MAX_VAR_NAME - 1] = '\0';
            strncpy(mem[idx].value, value, MAX_VAR_VALUE - 1);
            mem[idx].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    return false;
}

/* --------------------------------------------------------------------
   load_variable – retrieves a variable value
   -------------------------------------------------------------------- */
char *load_variable(PCB *p, MemoryWord *mem, const char *var_name)
{
    if (!p || !var_name || !p->in_memory || !mem)
        return NULL;

    int base = p->mem_start + PCB_WORDS;
    for (int i = 0; i < PROCESS_VAR_SLOTS; ++i) {
        int idx = base + i;
        if (idx >= MEMORY_SIZE) break;
        if (strncmp(mem[idx].name, var_name, MAX_VAR_NAME) == 0)
            return mem[idx].value;
    }
    return NULL;
}

/* --------------------------------------------------------------------
   load_process_into_memory – allocates and writes PCB + vars + code
   -------------------------------------------------------------------- */
bool load_process_into_memory(PCB *p, MemoryWord *mem, int *owners)
{
    if (!p || !mem || !owners) return false;
    if (p->in_memory) return true;

    if (!allocate_memory_block(p, mem, owners))
        return false;

    pcb_flush_to_memory(p, mem);

    /* Write instructions into memory */
    int instr_base = p->mem_start + PCB_WORDS + PROCESS_VAR_SLOTS;
    for (int i = 0; i < p->num_instructions; ++i) {
        int idx = instr_base + i;
        if (idx >= MEMORY_SIZE) break;
        snprintf(mem[idx].name, MAX_VAR_NAME, "INSTR_%d", i);
        strncpy(mem[idx].value, p->instructions[i], MAX_VAR_VALUE - 1);
        mem[idx].value[MAX_VAR_VALUE - 1] = '\0';
    }

    return true;
}

/* --------------------------------------------------------------------
   load_instruction – gets instruction text for PC
   -------------------------------------------------------------------- */
bool load_instruction(PCB *p, int pc, char *out_buffer, int max_size)
{
    if (!p || pc < 0 || pc >= p->num_instructions || !out_buffer || max_size <= 0)
        return false;

    strncpy(out_buffer, p->instructions[pc], (size_t)(max_size - 1));
    out_buffer[max_size - 1] = '\0';
    return true;
}

/* --------------------------------------------------------------------
   swap_out – saves variable states and unloads process from RAM
   -------------------------------------------------------------------- */
bool swap_out(PCB *p, MemoryWord *mem, int *owners)
{
    if (!p || !p->in_memory || !mem || !owners)
        return false;

    SwapData *sd = (SwapData *)malloc(sizeof(SwapData));
    if (sd) {
        int var_base = p->mem_start + PCB_WORDS;
        for (int i = 0; i < PROCESS_VAR_SLOTS; i++) {
            int idx = var_base + i;
            if (idx <= p->mem_end && idx < MEMORY_SIZE) {
                memcpy(&sd->vars[i], &mem[idx], sizeof(MemoryWord));
            } else {
                sd->vars[i].name[0]  = '\0';
                sd->vars[i].value[0] = '\0';
            }
        }
    }
    if (p->swap_data) free(p->swap_data);
    p->swap_data = sd;

    free_memory_block(p, mem, owners);
    return true;
}

/* --------------------------------------------------------------------
   swap_in – brings process back to RAM and restores its variables
   -------------------------------------------------------------------- */
bool swap_in(PCB *p, MemoryWord *mem, int *owners)
{
    if (!p || p->in_memory || !mem || !owners)
        return false;

    if (!load_process_into_memory(p, mem, owners))
        return false;

    SwapData *sd = (SwapData *)p->swap_data;
    if (sd) {
        int var_base = p->mem_start + PCB_WORDS;
        for (int i = 0; i < PROCESS_VAR_SLOTS; i++) {
            int idx = var_base + i;
            if (idx <= p->mem_end && idx < MEMORY_SIZE &&
                sd->vars[i].name[0] != '\0') {
                memcpy(&mem[idx], &sd->vars[i], sizeof(MemoryWord));
            }
        }
        free(sd);
        p->swap_data = NULL;
    }

    return true;
}

/* --------------------------------------------------------------------
   print_memory – debug utility
   -------------------------------------------------------------------- */
void print_memory(MemoryWord *mem, int *owners)
{
    if (!mem || !owners) return;
    fprintf(stderr, "-- Memory (%d words) --\n", MEMORY_SIZE);
    for (int i = 0; i < MEMORY_SIZE; ++i) {
        fprintf(stderr, "[%02d] owner=%3d name=%-8s value=%s\n",
                i, owners[i],
                mem[i].name[0]  ? mem[i].name  : "-",
                mem[i].value[0] ? mem[i].value : "-");
    }
}

