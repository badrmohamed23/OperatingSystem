#include "os_shared.h"

// Simple per-process variable table used only for Phase 2 stub execution
#define MAX_PROCESSES 10
#define MAX_VARS_PER_PROCESS 20

typedef struct
{
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} VarEntry;

typedef struct
{
    int pid; // 0 means unused
    VarEntry vars[MAX_VARS_PER_PROCESS];
} VarTable;

static VarTable var_tables[MAX_PROCESSES];

static VarTable *get_var_table_for_pid(int pid)
{
    if (pid <= 0)
        return NULL;

    // Look for existing table
    for (int i = 0; i < MAX_PROCESSES; ++i)
    {
        if (var_tables[i].pid == pid)
            return &var_tables[i];
    }

    // Find empty slot
    for (int i = 0; i < MAX_PROCESSES; ++i)
    {
        if (var_tables[i].pid == 0)
        {
            var_tables[i].pid = pid;
            for (int j = 0; j < MAX_VARS_PER_PROCESS; ++j)
            {
                var_tables[i].vars[j].name[0] = '\0';
                var_tables[i].vars[j].value[0] = '\0';
            }
            return &var_tables[i];
        }
    }

    return NULL; // no space
}

static void set_var_value(int pid, const char *name, const char *value)
{
    VarTable *vt = get_var_table_for_pid(pid);
    if (!vt || !name || !value)
        return;

    // If variable exists, overwrite
    for (int i = 0; i < MAX_VARS_PER_PROCESS; ++i)
    {
        if (vt->vars[i].name[0] != '\0' && strcmp(vt->vars[i].name, name) == 0)
        {
            strncpy(vt->vars[i].value, value, MAX_VAR_VALUE - 1);
            vt->vars[i].value[MAX_VAR_VALUE - 1] = '\0';
            return;
        }
    }

    // Otherwise, put into first empty slot
    for (int i = 0; i < MAX_VARS_PER_PROCESS; ++i)
    {
        if (vt->vars[i].name[0] == '\0')
        {
            strncpy(vt->vars[i].name, name, MAX_VAR_NAME - 1);
            vt->vars[i].name[MAX_VAR_NAME - 1] = '\0';

            strncpy(vt->vars[i].value, value, MAX_VAR_VALUE - 1);
            vt->vars[i].value[MAX_VAR_VALUE - 1] = '\0';
            return;
        }
    }

    printf("[Interpreter] Warning: variable table full for PID %d, cannot store '%s'.\n", pid, name);
}

static const char *get_var_value(int pid, const char *name)
{
    VarTable *vt = get_var_table_for_pid(pid);
    if (!vt || !name)
        return NULL;

    for (int i = 0; i < MAX_VARS_PER_PROCESS; ++i)
    {
        if (vt->vars[i].name[0] != '\0' && strcmp(vt->vars[i].name, name) == 0)
        {
            return vt->vars[i].value;
        }
    }

    return NULL;
}

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
    strncpy(line_buf, p->instructions[p->program_counter], MAX_INSTRUCTION_LEN - 1);
    line_buf[MAX_INSTRUCTION_LEN - 1] = '\0';

    trim_whitespace(line_buf);

    if (line_buf[0] == '\0')
    {
        p->program_counter++;
        if (p->program_counter >= p->num_instructions)
            p->state = FINISHED;
        return;
    }

    char *cmd = strtok(line_buf, " ");
    if (!cmd)
    {
        p->program_counter++;
        if (p->program_counter >= p->num_instructions)
            p->state = FINISHED;
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
            sys_input(input_buf, sizeof(input_buf));
            set_var_value(p->pid, var_name, input_buf);
        }
        else if (strcmp(src, "readFile") == 0)
        {
            char *file_var_name = strtok(NULL, " ");
            const char *file_name = get_var_value(p->pid, file_var_name);
            char file_buf[MAX_VAR_VALUE];
            if (file_name && sys_readFile(file_name, file_buf, sizeof(file_buf)))
            {
                set_var_value(p->pid, var_name, file_buf);
            }
            else
            {
                printf("[Interpreter] readFile failed for PID %d (filename var '%s').\n", p->pid, file_var_name ? file_var_name : "(null)");
            }
        }
        else
        {
            // Direct value assignment (e.g., assign x 5)
            set_var_value(p->pid, var_name, src);
        }
    }
    // ------------------------------------------------------
    // print x
    // ------------------------------------------------------
    else if (strcmp(cmd, "print") == 0)
    {
        char *var_name = strtok(NULL, " ");
        const char *val = get_var_value(p->pid, var_name);
        if (!val)
            val = "(undefined)";
        char buffer[MAX_VAR_VALUE + MAX_VAR_NAME + 32];
        snprintf(buffer, sizeof(buffer), "PID %d: %s = %s", p->pid, var_name ? var_name : "(null)", val);
        sys_print(buffer);
    }
    // ------------------------------------------------------
    // printFromTo x y
    // ------------------------------------------------------
    else if (strcmp(cmd, "printFromTo") == 0)
    {
        char *from_name = strtok(NULL, " ");
        char *to_name = strtok(NULL, " ");

        const char *from_val = get_var_value(p->pid, from_name);
        const char *to_val = get_var_value(p->pid, to_name);

        if (!from_val)
            from_val = "(undefined)";
        if (!to_val)
            to_val = "(undefined)";

        char buffer[MAX_VAR_VALUE * 2 + 64];
        snprintf(buffer, sizeof(buffer), "PID %d: from %s to %s\n", p->pid, from_val, to_val);
        sys_print(buffer);
    }
    // ------------------------------------------------------
    // writeFile a b  (a = filename var, b = data var)
    // ------------------------------------------------------
    else if (strcmp(cmd, "writeFile") == 0)
    {
        char *file_var_name = strtok(NULL, " ");
        char *data_var_name = strtok(NULL, " ");

        const char *file_name = get_var_value(p->pid, file_var_name);
        const char *data_val = get_var_value(p->pid, data_var_name);
        if (!file_name || !data_val)
        {
            printf("[Interpreter] writeFile missing filename or data for PID %d.\n", p->pid);
        }
        else
        {
            if (!sys_writeFile(file_name, data_val))
            {
                printf("[Interpreter] writeFile failed for PID %d (file '%s').\n", p->pid, file_name);
            }
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
}
