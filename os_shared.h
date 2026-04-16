#ifndef OS_SHARED_H
#define OS_SHARED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
   Constants
   ======================================================================== */
#define MEMORY_SIZE 40          // number of memory words
#define PCB_WORDS 4             // number of words reserved for PCB metadata in each block
#define PROCESS_VAR_SLOTS 3     // fixed variable slots per process
#define MAX_VAR_NAME 32         // max length of variable name
#define MAX_VAR_VALUE 256       // max length of variable value (string)
#define MAX_INSTRUCTION_LEN 256 // max length of one line of program code
#define MAX_FILENAME_LEN 128
#define MAX_USER_INPUT 256

/* ========================================================================
   Enumerations
   ======================================================================== */
typedef enum
{
   NEW,
   READY,
   RUNNING,
   BLOCKED,
   FINISHED
} State;

typedef enum
{
   RES_USER_INPUT,  // mutex for taking input from keyboard
   RES_USER_OUTPUT, // mutex for printing to screen
   RES_FILE         // mutex for file read/write
} ResourceType;

typedef enum
{
   SCHED_HRRN,
   SCHED_RR,
   SCHED_MLFQ
} SchedulerType;

/* ========================================================================
   Memory Word – stores one variable (name + value)
   ======================================================================== */
typedef struct
{
   char name[MAX_VAR_NAME];
   char value[MAX_VAR_VALUE];
} MemoryWord;

/* ========================================================================
   Process Control Block (PCB)
   ======================================================================== */
typedef struct PCB
{
   int pid;                             // process ID
   State state;                         // current state
   int program_counter;                 // index of next instruction to execute
   int mem_start;                       // starting memory word index (inclusive)
   int mem_end;                         // ending memory word index (inclusive)
   bool in_memory;                      // whether this process currently has memory allocated
   int swap_start;                      // starting swap word index if swapped out
   int waiting_time;                    // for HRRN: total time spent in ready queue
   int burst_time;                      // total instructions (service time for HRRN)
   int remaining_time;                  // remaining instructions during execution
   int priority_level;                  // for MLFQ: 0 (highest) .. 3 (lowest)
   char program_name[MAX_FILENAME_LEN]; // original program filename
   char **instructions;                 // array of strings, each = one instruction
   int num_instructions;                // number of instructions in the program
   int arrival_time;                    // when process arrives (for scheduling)
   int last_scheduled_time;             // last time it started running (for HRRN)
   struct PCB *next;                    // for linked lists (queues)
} PCB;

/* ========================================================================
   PCB <-> memory helpers
   ------------------------------------------------------------------------
   The project spec requires that the PCB fields live inside the simulated
   memory. We therefore reserve the first PCB_WORDS in each allocated block
   to hold PID, state, PC, and bounds. These helpers keep the in-memory
   representation in sync with the PCB struct that the scheduler uses.
   ======================================================================== */

// Write the PCB's core fields (pid, state, PC, bounds) into its
// reserved words in memory, if the process currently has memory.
void pcb_flush_to_memory(PCB *p);

/* ========================================================================
   Mutex structure (used by Member 4)
   ======================================================================== */

#define MAX_WAITING_PIDS 10

typedef struct
{
   int locked;              // 1 = locked, 0 = free
   PCB *waiting_queue_head; // for later phases (real queues)
   PCB *waiting_queue_tail; // not used yet in Phase 3

   // Phase 3: simple fixed-size array of waiting PIDs (no real blocking yet)
   int waiting_pids[MAX_WAITING_PIDS];
   int waiting_count;
} Mutex;

/* ========================================================================
   Shared memory and memory management API (Member 1)
   ======================================================================== */

// Global simulated memory of fixed size
extern MemoryWord memory[MEMORY_SIZE];
extern int mem_owner[MEMORY_SIZE]; // -1 = free, otherwise PID

// Initialize memory array and allocation table
void init_memory(void);

// First-fit allocation of a fixed-size block (e.g., 3 words per process)
// Returns true on success, false if no suitable block exists
bool allocate_memory_block(PCB *p);

// Free the memory block owned by the given process
void free_memory_block(PCB *p);

// Store a variable (name, value) within the process's memory block
// Overwrites existing variable if it already exists; otherwise uses an empty slot
bool store_variable(PCB *p, char *var_name, char *value);

// Load a variable's value from the process's memory block; returns NULL if not found
char *load_variable(PCB *p, char *var_name);

// Debug helper: print all memory words and their owners
void print_memory(void);

// Debug helper: print the swap-space ("disk") layout
void print_swap_space(void);

// Simple curses-based TUI helpers
void ui_init(int enable);
void ui_shutdown(void);
void ui_draw_step(int current_time, SchedulerType algo, PCB *running);

// Swap support for Phase 5
bool swap_out(PCB *p);
bool swap_in(PCB *p);

// Build an in-memory image for a process:
// [PCB_WORDS][PROCESS_VAR_SLOTS][INSTR_0..INSTR_n-1]
bool load_process_into_memory(PCB *p);

// Fetch instruction text by logical PC from the process memory image.
bool load_instruction(PCB *p, int pc, char *out_buffer, int max_size);

/* ========================================================================
   Process creation and interpreter (Member 1)
   ======================================================================== */

// Create a process from a program text file; reads all lines into PCB->instructions
// and initializes PCB fields. Returns NULL on error.
PCB *create_process(int pid, const char *program_filename, int arrival_time);

// Execute the current instruction of the given process and advance its PC.
// When the last instruction is executed, sets p->state = FINISHED.
void execute_instruction(PCB *p);

/* ========================================================================
   System call interface (Member 4)
   ======================================================================== */

// Basic I/O system calls
void sys_print(const char *text);
void sys_input(char *buffer, int size);

// File system calls; buffers must be large enough for contents
// Returns true on success, false on error.
bool sys_readFile(const char *filename, char *out_buffer, int max_size);
bool sys_writeFile(const char *filename, const char *data);

// Memory access wrappers around Member 2's memory functions
// Write a variable value into the process's memory block
bool sys_write_mem(PCB *p, const char *var_name, const char *value);

// Read a variable value from the process's memory block into out_buffer
// Returns true on success (variable found), false otherwise.
bool sys_read_mem(PCB *p, const char *var_name, char *out_buffer, int max_size);

// Mutex and semaphore-style operations (busy-wait for now)
void init_mutexes(void);
void sem_wait(ResourceType res, PCB *p);
void sem_signal(ResourceType res, PCB *p);

/* ========================================================================
   Scheduler & queues (Member 3)
   ======================================================================== */

void init_scheduler(SchedulerType algo);
void add_to_ready(PCB *p);
void add_to_blocked(ResourceType res, PCB *p);
PCB *unblock_one(ResourceType res);
PCB *get_next_process(void);
void scheduler_on_tick(PCB *running);
void print_queues(void);

#endif /* OS_SHARED_H */