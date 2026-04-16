#include "os_shared.h"

// Global memory and allocation table definitions
MemoryWord memory[MEMORY_SIZE];
int mem_owner[MEMORY_SIZE]; // -1 = free, otherwise PID

// Track swapped-out processes and their backing files.
static int swapped_pids[MEMORY_SIZE];
static int swapped_count = 0;

static void clear_word(int idx)
{
    memory[idx].name[0] = '\0';
    memory[idx].value[0] = '\0';
}

static int required_words_for_process(const PCB *p)
{
    if (!p)
        return 0;

    int instruction_words = (p->num_instructions > 0) ? p->num_instructions : 0;
    return PCB_WORDS + PROCESS_VAR_SLOTS + instruction_words;
}

static int find_free_block(int needed)
{
    if (needed <= 0 || needed > MEMORY_SIZE)
        return -1;

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
            return i;
    }
    return -1;
}

static void get_swap_filename(int pid, char *out, int out_size)
{
    snprintf(out, out_size, "swap_pid_%d.mem", pid);
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static bool read_line_strip(FILE *f, char *out, int out_size)
{
    if (!f || !out || out_size <= 0)
        return false;

    if (!fgets(out, out_size, f))
        return false;

    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
    {
        out[len - 1] = '\0';
        --len;
    }

    return true;
}

static void add_swapped_pid(int pid)
{
    for (int i = 0; i < swapped_count; ++i)
    {
        if (swapped_pids[i] == pid)
            return;
    }

    if (swapped_count < MEMORY_SIZE)
    {
        swapped_pids[swapped_count++] = pid;
    }
}

static void remove_swapped_pid(int pid)
{
    for (int i = 0; i < swapped_count; ++i)
    {
        if (swapped_pids[i] == pid)
        {
            for (int j = i; j + 1 < swapped_count; ++j)
                swapped_pids[j] = swapped_pids[j + 1];
            swapped_count--;
            return;
        }
    }
}

static void print_swap_file_for_pid(int pid)
{
    char path[64];
    get_swap_filename(pid, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f)
    {
        printf("PID %d -> missing swap file '%s'\n", pid, path);
        return;
    }

    char line[512];
    printf("PID %d -> %s\n", pid, path);

    if (read_line_strip(f, line, sizeof(line)))
        printf("  %s\n", line);
    if (read_line_strip(f, line, sizeof(line)))
        printf("  %s\n", line);

    printf("  WORDS:\n");
    while (read_line_strip(f, line, sizeof(line)))
    {
        printf("    %s\n", line);
    }

    fclose(f);
}

void init_memory(void)
{
    for (int i = 0; i < MEMORY_SIZE; ++i)
    {
        clear_word(i);
        mem_owner[i] = -1;
    }

    swapped_count = 0;
}

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

    strncpy(memory[base + 0].name, "PID", MAX_VAR_NAME - 1);
    memory[base + 0].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d", p->pid);
    strncpy(memory[base + 0].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 0].value[MAX_VAR_VALUE - 1] = '\0';

    strncpy(memory[base + 1].name, "STATE", MAX_VAR_NAME - 1);
    memory[base + 1].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d", (int)p->state);
    strncpy(memory[base + 1].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 1].value[MAX_VAR_VALUE - 1] = '\0';

    strncpy(memory[base + 2].name, "PC", MAX_VAR_NAME - 1);
    memory[base + 2].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d", p->program_counter);
    strncpy(memory[base + 2].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 2].value[MAX_VAR_VALUE - 1] = '\0';

    strncpy(memory[base + 3].name, "BOUNDS", MAX_VAR_NAME - 1);
    memory[base + 3].name[MAX_VAR_NAME - 1] = '\0';
    snprintf(buf, sizeof(buf), "%d-%d", p->mem_start, p->mem_end);
    strncpy(memory[base + 3].value, buf, MAX_VAR_VALUE - 1);
    memory[base + 3].value[MAX_VAR_VALUE - 1] = '\0';
}

bool allocate_memory_block(PCB *p)
{
    if (!p)
        return false;

    int needed = required_words_for_process(p);
    int start = find_free_block(needed);
    if (start < 0)
        return false;

    for (int j = 0; j < needed; ++j)
    {
        int idx = start + j;
        mem_owner[idx] = p->pid;
        clear_word(idx);
    }

    p->mem_start = start;
    p->mem_end = start + needed - 1;
    p->in_memory = true;
    pcb_flush_to_memory(p);
    return true;
}

bool load_process_into_memory(PCB *p)
{
    if (!p)
        return false;

    if (!allocate_memory_block(p))
        return false;

    int instr_start = p->mem_start + PCB_WORDS + PROCESS_VAR_SLOTS;

    for (int i = 0; i < p->num_instructions; ++i)
    {
        int idx = instr_start + i;
        if (idx > p->mem_end)
            return false;

        snprintf(memory[idx].name, MAX_VAR_NAME, "INSTR_%d", i);
        strncpy(memory[idx].value, p->instructions[i], MAX_VAR_VALUE - 1);
        memory[idx].value[MAX_VAR_VALUE - 1] = '\0';
    }

    pcb_flush_to_memory(p);
    return true;
}

bool load_instruction(PCB *p, int pc, char *out_buffer, int max_size)
{
    if (!p || !out_buffer || max_size <= 0)
        return false;

    if (!p->in_memory)
        return false;

    if (pc < 0 || pc >= p->num_instructions)
        return false;

    int idx = p->mem_start + PCB_WORDS + PROCESS_VAR_SLOTS + pc;
    if (idx < p->mem_start || idx > p->mem_end)
        return false;

    if (mem_owner[idx] != p->pid)
        return false;

    strncpy(out_buffer, memory[idx].value, max_size - 1);
    out_buffer[max_size - 1] = '\0';
    return true;
}

bool swap_out(PCB *p)
{
    if (!p || !p->in_memory)
        return false;

    int block_words = p->mem_end - p->mem_start + 1;
    if (block_words <= 0)
        return false;

    char path[64];
    get_swap_filename(p->pid, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    fprintf(f, "PID %d\n", p->pid);
    fprintf(f, "WORDS %d\n", block_words);

    for (int j = 0; j < block_words; ++j)
    {
        int src = p->mem_start + j;
        fprintf(f, "%d|%s|%s\n", j, memory[src].name, memory[src].value);
    }

    fclose(f);

    int old_start = p->mem_start;
    int old_end = p->mem_end;

    free_memory_block(p);
    p->swap_start = 0;
    p->in_memory = false;

    add_swapped_pid(p->pid);

    printf("[Memory] Swapped out PID %d from mem[%d..%d] to disk file '%s'\n",
           p->pid, old_start, old_end, path);
    return true;
}

bool swap_in(PCB *p)
{
    if (!p)
        return false;

    if (p->in_memory)
        return true;

    char path[64];
    get_swap_filename(p->pid, path, sizeof(path));
    if (!file_exists(path))
        return false;

    if (!allocate_memory_block(p))
        return false;

    FILE *f = fopen(path, "r");
    if (!f)
    {
        free_memory_block(p);
        return false;
    }

    char line[512];
    if (!read_line_strip(f, line, sizeof(line)))
    {
        fclose(f);
        free_memory_block(p);
        return false;
    }

    int pid_on_disk = -1;
    if (sscanf(line, "PID %d", &pid_on_disk) != 1 || pid_on_disk != p->pid)
    {
        fclose(f);
        free_memory_block(p);
        return false;
    }

    if (!read_line_strip(f, line, sizeof(line)))
    {
        fclose(f);
        free_memory_block(p);
        return false;
    }

    int words_on_disk = 0;
    if (sscanf(line, "WORDS %d", &words_on_disk) != 1)
    {
        fclose(f);
        free_memory_block(p);
        return false;
    }

    int allocated_words = p->mem_end - p->mem_start + 1;
    if (words_on_disk != allocated_words)
    {
        fclose(f);
        free_memory_block(p);
        return false;
    }

    for (int j = 0; j < words_on_disk; ++j)
    {
        if (!read_line_strip(f, line, sizeof(line)))
        {
            fclose(f);
            free_memory_block(p);
            return false;
        }

        char *first = strchr(line, '|');
        if (!first)
        {
            fclose(f);
            free_memory_block(p);
            return false;
        }
        char *second = strchr(first + 1, '|');
        if (!second)
        {
            fclose(f);
            free_memory_block(p);
            return false;
        }

        *first = '\0';
        *second = '\0';

        const char *name = first + 1;
        const char *value = second + 1;

        int dst = p->mem_start + j;
        strncpy(memory[dst].name, name, MAX_VAR_NAME - 1);
        memory[dst].name[MAX_VAR_NAME - 1] = '\0';

        strncpy(memory[dst].value, value, MAX_VAR_VALUE - 1);
        memory[dst].value[MAX_VAR_VALUE - 1] = '\0';
    }

    fclose(f);

    pcb_flush_to_memory(p);

    remove(path);
    remove_swapped_pid(p->pid);

    p->swap_start = -1;
    p->in_memory = true;

    printf("[Memory] Swapped in PID %d to mem[%d..%d] from disk file '%s'\n",
           p->pid, p->mem_start, p->mem_end, path);
    return true;
}

void free_memory_block(PCB *p)
{
    if (!p)
        return;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return;

    for (int i = p->mem_start; i <= p->mem_end; ++i)
    {
        if (mem_owner[i] == p->pid)
        {
            mem_owner[i] = -1;
        }
        clear_word(i);
    }

    p->mem_start = -1;
    p->mem_end = -1;
    p->in_memory = false;
}

bool store_variable(PCB *p, char *var_name, char *value)
{
    if (!p || !var_name || !value)
        return false;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return false;

    int var_start = p->mem_start + PCB_WORDS;
    int var_end = var_start + PROCESS_VAR_SLOTS - 1;

    if (var_end > p->mem_end)
        return false;

    for (int i = var_start; i <= var_end; ++i)
    {
        if (mem_owner[i] != p->pid)
            continue;

        if (memory[i].name[0] != '\0' && strcmp(memory[i].name, var_name) == 0)
        {
            strncpy(memory[i].name, var_name, MAX_VAR_NAME - 1);
            memory[i].name[MAX_VAR_NAME - 1] = '\0';

            strncpy(memory[i].value, value, MAX_VAR_VALUE - 1);
            memory[i].value[MAX_VAR_VALUE - 1] = '\0';
            return true;
        }
    }

    for (int i = var_start; i <= var_end; ++i)
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

    return false;
}

char *load_variable(PCB *p, char *var_name)
{
    if (!p || !var_name)
        return NULL;

    if (p->mem_start < 0 || p->mem_end < p->mem_start || p->mem_end >= MEMORY_SIZE)
        return NULL;

    int var_start = p->mem_start + PCB_WORDS;
    int var_end = var_start + PROCESS_VAR_SLOTS - 1;

    if (var_end > p->mem_end)
        return NULL;

    for (int i = var_start; i <= var_end; ++i)
    {
        if (mem_owner[i] != p->pid)
            continue;

        if (memory[i].name[0] != '\0' && strcmp(memory[i].name, var_name) == 0)
        {
            return memory[i].value;
        }
    }

    return NULL;
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
    printf("==================== SWAP DISK DUMP ==================\n");

    if (swapped_count == 0)
    {
        printf("(empty)\n");
    }
    else
    {
        for (int i = 0; i < swapped_count; ++i)
        {
            print_swap_file_for_pid(swapped_pids[i]);
        }
    }

    printf("======================================================\n");
}
