#include "os_shared.h"

// =====================================================================
// Internal mutexes and helpers
// =====================================================================

static Mutex userInputMutex;
static Mutex userOutputMutex;
static Mutex fileMutex;

static Mutex *get_mutex_for_resource(ResourceType res)
{
    switch (res)
    {
    case RES_USER_INPUT:
        return &userInputMutex;
    case RES_USER_OUTPUT:
        return &userOutputMutex;
    case RES_FILE:
        return &fileMutex;
    default:
        return NULL;
    }
}

void init_mutexes(void)
{
    Mutex *m_list[3] = {&userInputMutex, &userOutputMutex, &fileMutex};
    for (int i = 0; i < 3; ++i)
    {
        Mutex *m = m_list[i];
        m->locked = 0;
        m->waiting_queue_head = NULL;
        m->waiting_queue_tail = NULL;
        m->waiting_count = 0;
        for (int j = 0; j < MAX_WAITING_PIDS; ++j)
        {
            m->waiting_pids[j] = -1;
        }
    }
}

void sem_wait(ResourceType res, PCB *p)
{
    Mutex *m = get_mutex_for_resource(res);
    if (!m)
        return;

    // If free, acquire immediately
    if (m->locked == 0)
    {
        m->locked = 1;
        return;
    }

    // Otherwise, block the process on the appropriate blocked queue
    if (p)
    {
        add_to_blocked(res, p);
        p->state = BLOCKED;
    }
}

void sem_signal(ResourceType res, PCB *p)
{
    (void)p; // unused for now
    Mutex *m = get_mutex_for_resource(res);
    if (!m)
        return;

    // Try to unblock one process waiting on this resource
    PCB *unblocked = unblock_one(res);
    if (!unblocked)
    {
        // No one waiting; release the mutex
        m->locked = 0;
    }
    else
    {
        // Hand-off style: keep mutex locked; unblocked process conceptually owns it
        m->locked = 1;
    }
}

// =====================================================================
// Basic I/O system calls (wrapped with mutexes)
// =====================================================================

void sys_print(const char *text)
{
    if (text)
        printf("%s", text);
}

void sys_input(char *buffer, int size)
{
    if (!buffer || size <= 0)
        return;

    if (fgets(buffer, size, stdin) == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    // Strip trailing newline if present
    size_t len = strlen(buffer);
    if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
    {
        buffer[len - 1] = '\0';
        len--;
        if (len > 0 && buffer[len - 1] == '\r')
        {
            buffer[len - 1] = '\0';
        }
    }
}

// =====================================================================
// File system calls (wrapped with file mutex)
// =====================================================================

bool sys_readFile(const char *filename, char *out_buffer, int max_size)
{
    if (!filename || !out_buffer || max_size <= 0)
        return false;

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        printf("[sys_readFile] Failed to open file '%s' for reading.\n", filename);
        out_buffer[0] = '\0';
        return false;
    }

    int total = 0;
    int c;
    while ((c = fgetc(f)) != EOF && total < max_size - 1)
    {
        out_buffer[total++] = (char)c;
    }
    out_buffer[total] = '\0';

    fclose(f);
    return true;
}

bool sys_writeFile(const char *filename, const char *data)
{
    if (!filename || !data)
        return false;

    FILE *f = fopen(filename, "w");
    if (!f)
    {
        printf("[sys_writeFile] Failed to open file '%s' for writing.\n", filename);
        return false;
    }

    size_t written = fwrite(data, 1, strlen(data), f);
    fclose(f);

    if (written < strlen(data))
    {
        printf("[sys_writeFile] Warning: not all data written to '%s'.\n", filename);
        return false;
    }

    return true;
}

// =====================================================================
// Memory wrappers around Member 2's functions
// =====================================================================

bool sys_write_mem(PCB *p, const char *var_name, const char *value)
{
    if (!p || !var_name || !value)
        return false;

    // store_variable already handles overwriting or using an empty slot
    return store_variable(p, (char *)var_name, (char *)value);
}

bool sys_read_mem(PCB *p, const char *var_name, char *out_buffer, int max_size)
{
    if (!p || !var_name || !out_buffer || max_size <= 0)
        return false;

    char *val = load_variable(p, (char *)var_name);
    if (!val)
    {
        out_buffer[0] = '\0';
        return false;
    }

    strncpy(out_buffer, val, max_size - 1);
    out_buffer[max_size - 1] = '\0';
    return true;
}

// =====================================================================
// Standalone test for Member 4 (compile with -DTEST_SYSCALL_MAIN)
// =====================================================================

#ifdef TEST_SYSCALL_MAIN
int main(void)
{
    // Test sys_print and sys_input
    char input_buf[128];
    init_mutexes();

    sys_print("Enter a line: ");
    sys_input(input_buf, sizeof(input_buf));
    sys_print("You entered: ");
    sys_print(input_buf);
    sys_print("\n");

    // Test file read/write
    const char *test_filename = "syscall_test.txt";
    const char *file_data = "Hello from sys_writeFile!\nSecond line.\n";

    if (sys_writeFile(test_filename, file_data))
    {
        char file_buf[512];
        if (sys_readFile(test_filename, file_buf, sizeof(file_buf)))
        {
            sys_print("Read back from file: \n");
            sys_print(file_buf);
        }
    }

    // Test memory wrappers using Member 2's memory system
    PCB p1;
    p1.pid = 1;
    p1.mem_start = -1;
    p1.mem_end = -1;

    init_memory();
    if (!allocate_memory_block(&p1))
    {
        printf("Failed to allocate memory block for PID %d\n", p1.pid);
        return 1;
    }

    sys_write_mem(&p1, "var1", "value1");
    sys_write_mem(&p1, "var2", "value2");

    char mem_buf[256];
    if (sys_read_mem(&p1, "var1", mem_buf, sizeof(mem_buf)))
    {
        printf("sys_read_mem var1 = %s\n", mem_buf);
    }

    if (sys_read_mem(&p1, "var2", mem_buf, sizeof(mem_buf)))
    {
        printf("sys_read_mem var2 = %s\n", mem_buf);
    }

    print_memory();

    free_memory_block(&p1);

    return 0;
}
#endif
