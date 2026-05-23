#include "os_shared.h"

// ------------------------------------------------------------------------
// ------------------------------------------------------------------------
// Helper: trim leading/trailing whitespace
// ------------------------------------------------------------------------

static void trim_whitespace(char *s)
{
    if (!s)
        return;

    // Trim leading spaces
    char *start = s;
    while (*start == ' ' || *start == '\t')
        ++start;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    // Trim trailing spaces
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n' || s[len - 1] == '\r'))
    {
        s[len - 1] = '\0';
        --len;
    }
}

// ------------------------------------------------------------------------
// Main interpreter entry point
// ------------------------------------------------------------------------

void execute_instruction(PCB *p)
{
    if (!p)
        return;

    if (p->program_counter < 0 || p->program_counter >= p->num_instructions)
    {
        p->state = FINISHED;
        return;
    }

    char line_buf[MAX_INSTRUCTION_LEN];
    if (!load_instruction(p, p->program_counter, line_buf, sizeof(line_buf)))
    {
        printf("[Interpreter] Failed to fetch instruction PC=%d for PID %d from memory.\n",
               p->program_counter, p->pid);
        p->state = FINISHED;
        return;
    }

    trim_whitespace(line_buf);

    if (line_buf[0] == '\0')
    {
        p->program_counter++;
        if (p->program_counter >= p->num_instructions)
            p->state = FINISHED;
        if (p->in_memory)
            pcb_flush_to_memory(p);
        return;
    }

    char *cmd = strtok(line_buf, " ");
    if (!cmd)
    {
        p->program_counter++;
        if (p->program_counter >= p->num_instructions)
            p->state = FINISHED;
        if (p->in_memory)
            pcb_flush_to_memory(p);
        return;
    }

    // ------------------------------------------------------
    // semWait / semSignal
    // ------------------------------------------------------
    if (strcmp(cmd, "semWait") == 0)
    {
        char *res = strtok(NULL, " ");
        if (res && strcmp(res, "userInput") == 0)
            sem_wait(RES_USER_INPUT, p);
        else if (res && strcmp(res, "userOutput") == 0)
            sem_wait(RES_USER_OUTPUT, p);
        else if (res && strcmp(res, "file") == 0)
            sem_wait(RES_FILE, p);
        else
            printf("[Interpreter] Unknown semaphore resource '%s' for PID %d.\n", res ? res : "(null)", p->pid);
    }
    else if (strcmp(cmd, "semSignal") == 0)
    {
        char *res = strtok(NULL, " ");
        if (res && strcmp(res, "userInput") == 0)
            sem_signal(RES_USER_INPUT, p);
        else if (res && strcmp(res, "userOutput") == 0)
            sem_signal(RES_USER_OUTPUT, p);
        else if (res && strcmp(res, "file") == 0)
            sem_signal(RES_FILE, p);
        else
            printf("[Interpreter] Unknown semaphore resource '%s' for PID %d.\n", res ? res : "(null)", p->pid);
    }
    // ------------------------------------------------------
    // assign x input / assign x readFile y / assign x 10
    // ------------------------------------------------------
    else if (strcmp(cmd, "assign") == 0)
    {
        char *var_name = strtok(NULL, " ");
        char *src = strtok(NULL, " ");

        if (!var_name || !src)
        {
            printf("[Interpreter] Malformed assign instruction for PID %d.\n", p->pid);
        }
        else if (strcmp(src, "input") == 0)
        {
            char input_buf[MAX_VAR_VALUE];
            // Show a clear prompt so the user knows input is required
            char prompt[MAX_VAR_NAME + 64];
            snprintf(prompt, sizeof(prompt), "Please enter value for %s: ", var_name);
            sys_print(prompt);
            sys_input(input_buf, sizeof(input_buf));
            if (!sys_write_mem(p, var_name, input_buf))
            {
                printf("[Interpreter] Failed to store '%s' for PID %d.\n", var_name, p->pid);
            }
        }
        else if (strcmp(src, "readFile") == 0)
        {
            char *file_var_name = strtok(NULL, " ");
            char file_name[MAX_VAR_VALUE];
            char file_buf[MAX_VAR_VALUE];

            if (!file_var_name || !sys_read_mem(p, file_var_name, file_name, sizeof(file_name)))
            {
                printf("[Interpreter] readFile failed for PID %d (filename var '%s').\n", p->pid, file_var_name ? file_var_name : "(null)");
            }
            else if (!sys_readFile(file_name, file_buf, sizeof(file_buf)))
            {
                printf("[Interpreter] readFile failed for PID %d: cannot open '%s'.\n", p->pid, file_name);
            }
            else if (!sys_write_mem(p, var_name, file_buf))
            {
                printf("[Interpreter] Failed to store '%s' for PID %d.\n", var_name, p->pid);
            }
        }
        else
        {
            // Direct value assignment (e.g., assign x 5)
            if (!sys_write_mem(p, var_name, src))
            {
                printf("[Interpreter] Failed to store '%s' for PID %d.\n", var_name, p->pid);
            }
        }
    }
    // ------------------------------------------------------
    // print x
    // ------------------------------------------------------
    else if (strcmp(cmd, "print") == 0)
    {
        char *var_name = strtok(NULL, " ");
        char value_buf[MAX_VAR_VALUE];
        const char *val = "(undefined)";

        if (var_name && sys_read_mem(p, var_name, value_buf, sizeof(value_buf)))
            val = value_buf;

        char buffer[MAX_VAR_VALUE + MAX_VAR_NAME + 32];
        snprintf(buffer, sizeof(buffer), "PID %d: %s = %s\n", p->pid, var_name ? var_name : "(null)", val);
        sys_print(buffer);
    }
    // ------------------------------------------------------
    // printFromTo x y
    // ------------------------------------------------------
    else if (strcmp(cmd, "printFromTo") == 0)
    {
        char *from_name = strtok(NULL, " ");
        char *to_name = strtok(NULL, " ");
        char from_buf[MAX_VAR_VALUE];
        char to_buf[MAX_VAR_VALUE];
        if (!from_name || !to_name ||
            !sys_read_mem(p, from_name, from_buf, sizeof(from_buf)) ||
            !sys_read_mem(p, to_name, to_buf, sizeof(to_buf)))
        {
            printf("[Interpreter] printFromTo missing bounds for PID %d.\n", p->pid);
        }
        else
        {
            int from_val = atoi(from_buf);
            int to_val = atoi(to_buf);

            if (from_val <= to_val)
            {
                for (int i = from_val + 1; i < to_val; ++i)
                {
                    char out[64];
                    snprintf(out, sizeof(out), "%d\n", i);
                    sys_print(out);
                }
            }
            else
            {
                for (int i = from_val - 1; i > to_val; --i)
                {
                    char out[64];
                    snprintf(out, sizeof(out), "%d\n", i);
                    sys_print(out);
                }
            }
        }
    }
    // ------------------------------------------------------
    // writeFile a b  (a = filename var, b = data var)
    // ------------------------------------------------------
    else if (strcmp(cmd, "writeFile") == 0)
    {
        char *file_var_name = strtok(NULL, " ");
        char *data_var_name = strtok(NULL, " ");
        char file_name[MAX_VAR_VALUE];
        char data_val[MAX_VAR_VALUE];

        if (!file_var_name || !data_var_name ||
            !sys_read_mem(p, file_var_name, file_name, sizeof(file_name)) ||
            !sys_read_mem(p, data_var_name, data_val, sizeof(data_val)))
        {
            printf("[Interpreter] writeFile missing filename or data for PID %d.\n", p->pid);
        }
        else if (!sys_writeFile(file_name, data_val))
        {
            printf("[Interpreter] writeFile failed for PID %d (file '%s').\n", p->pid, file_name);
        }
    }
    else
    {
        printf("[Interpreter] Unknown instruction for PID %d: %s\n", p->pid, cmd);
    }

    // Advance program counter and mark finished if at end
    p->program_counter++;
    if (p->program_counter >= p->num_instructions)
    {
        p->state = FINISHED;
    }

    if (p->in_memory)
    {
        pcb_flush_to_memory(p);
    }
}
