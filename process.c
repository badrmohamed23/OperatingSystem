#include "os_shared.h"

// Helper to trim trailing newline and carriage return characters
static void trim_newline(char *s)
{
    if (s == NULL)
        return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
    {
        s[len - 1] = '\0';
        --len;
    }
}

PCB *create_process(int pid, const char *program_filename, int arrival_time)
{
    if (program_filename == NULL)
        return NULL;

    FILE *f = fopen(program_filename, "r");
    if (!f)
    {
        printf("Failed to open program file: %s\n", program_filename);
        return NULL;
    }

    PCB *p = (PCB *)malloc(sizeof(PCB));
    if (!p)
    {
        printf("Failed to allocate PCB for program %s\n", program_filename);
        fclose(f);
        return NULL;
    }

    // Initialize basic PCB fields
    p->pid = pid;
    p->state = NEW;
    p->program_counter = 0;
    p->mem_start = -1;
    p->mem_end = -1;
    p->in_memory = false;
    p->swap_start = -1;
    p->waiting_time = 0;
    p->burst_time = 0; // will set after reading instructions
    p->instructions = NULL;
    p->num_instructions = 0;
    p->arrival_time = arrival_time;
    p->last_scheduled_time = arrival_time;
    p->next = NULL;

    // Dynamic array of instruction strings
    int capacity = 0;
    char buffer[MAX_INSTRUCTION_LEN];

    while (fgets(buffer, sizeof(buffer), f) != NULL)
    {
        trim_newline(buffer);

        // Skip completely empty lines
        if (buffer[0] == '\0')
            continue;

        if (p->num_instructions >= capacity)
        {
            int new_capacity = (capacity == 0) ? 4 : capacity * 2;
            char **new_instr = (char **)realloc(p->instructions, new_capacity * sizeof(char *));
            if (!new_instr)
            {
                printf("Failed to allocate instruction array for program %s\n", program_filename);
                // Clean up allocated instructions so far
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
        if (!line)
        {
            printf("Failed to allocate memory for instruction in program %s\n", program_filename);
            // Clean up allocated instructions so far
            for (int i = 0; i < p->num_instructions; ++i)
                free(p->instructions[i]);
            free(p->instructions);
            free(p);
            fclose(f);
            return NULL;
        }

        strcpy(line, buffer);
        p->instructions[p->num_instructions++] = line;
    }

    fclose(f);

    p->burst_time = p->num_instructions;

    printf("Created process PID=%d from '%s' with %d instructions, arrival=%d\n",
           p->pid, program_filename, p->num_instructions, p->arrival_time);

    return p;
}
