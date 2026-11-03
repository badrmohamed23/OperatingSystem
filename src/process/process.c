/* src/process/process.c – PCB creation and destruction */
#include "os_shared.h"

static void trim_newline(char *s)
{
    if (s == NULL) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        --len;
    }
}

PCB *create_process(int pid, const char *program_filename, int arrival_time)
{
    if (program_filename == NULL) return NULL;

    FILE *f = fopen(program_filename, "r");
    if (!f) {
        fprintf(stderr, "[Error] Failed to open program file: %s\n", program_filename);
        return NULL;
    }

    PCB *p = (PCB *)calloc(1, sizeof(PCB));
    if (!p) {
        fprintf(stderr, "[Error] Failed to allocate PCB for program %s\n", program_filename);
        fclose(f);
        return NULL;
    }

    p->pid              = pid;
    p->state            = NEW;
    p->program_counter  = 0;
    p->mem_start        = -1;
    p->mem_end          = -1;
    p->in_memory        = false;
    p->swap_data        = NULL;
    p->waiting_time     = 0;
    p->burst_time       = 0;
    p->remaining_time   = 0;
    p->priority_level   = 0;
    strncpy(p->program_name, program_filename, MAX_FILENAME_LEN - 1);
    p->program_name[MAX_FILENAME_LEN - 1] = '\0';
    p->instructions     = NULL;
    p->num_instructions = 0;
    p->arrival_time     = arrival_time;
    p->completion_time  = -1;
    p->first_run_time   = -1;
    p->turnaround_time  = 0;
    p->response_time    = 0;
    p->next             = NULL;

    int capacity = 0;
    char buffer[MAX_INSTRUCTION_LEN];

    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        trim_newline(buffer);
        if (buffer[0] == '\0') continue;

        if (p->num_instructions >= capacity) {
            int new_capacity = (capacity == 0) ? 8 : capacity * 2;
            char **new_instr = (char **)realloc(p->instructions,
                                                (size_t)new_capacity * sizeof(char *));
            if (!new_instr) {
                fprintf(stderr, "[Error] Failed to allocate instruction array for %s\n", program_filename);
                for (int i = 0; i < p->num_instructions; ++i)
                    free(p->instructions[i]);
                free(p->instructions);
                free(p);
                fclose(f);
                return NULL;
            }
            p->instructions = new_instr;
            capacity = new_capacity;
        }

        size_t len = strlen(buffer);
        char *line = (char *)malloc(len + 1);
        if (!line) {
            for (int i = 0; i < p->num_instructions; ++i)
                free(p->instructions[i]);
            free(p->instructions);
            free(p);
            fclose(f);
            return NULL;
        }
        memcpy(line, buffer, len + 1);
        p->instructions[p->num_instructions++] = line;
    }

    fclose(f);

    p->burst_time     = p->num_instructions;
    p->remaining_time = p->num_instructions;

    return p;
}

void destroy_process(PCB *p)
{
    if (!p) return;
    if (p->instructions) {
        for (int i = 0; i < p->num_instructions; ++i) {
            free(p->instructions[i]);
        }
        free(p->instructions);
        p->instructions = NULL;
    }
    if (p->swap_data) {
        free(p->swap_data);
        p->swap_data = NULL;
    }
    free(p);
}

