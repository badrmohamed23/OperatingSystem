#ifndef OS_SHARED_H
#define OS_SHARED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
   Constants
   ======================================================================== */
#define MEMORY_SIZE 40          /* number of memory words in simulated RAM    */
#define PCB_WORDS 4             /* words reserved for PCB metadata in block   */
#define PROCESS_VAR_SLOTS 3     /* fixed variable slots per process in RAM    */
#define MAX_VAR_NAME 32         /* max length of variable name                */
#define MAX_VAR_VALUE 256       /* max length of variable value               */
#define MAX_INSTRUCTION_LEN 256 /* max length of one line of program code     */
#define MAX_FILENAME_LEN 128
#define MAX_USER_INPUT 256
#define MAX_PROCESSES 16
#define MAX_EVENTS 4096
#define MAX_INPUT_VARS 16       /* max input vars per process (batch mode)    */
#define RES_COUNT 3             /* number of resource types                   */

/* ========================================================================
   Enumerations
   ======================================================================== */
typedef enum {
    NEW = 0,
    READY = 1,
    RUNNING = 2,
    BLOCKED = 3,
    FINISHED = 4
} State;

typedef enum {
    RES_USER_INPUT  = 0,   /* mutex for reading user input               */
    RES_USER_OUTPUT = 1,   /* mutex for writing to screen/output         */
    RES_FILE        = 2    /* mutex for file read/write operations       */
} ResourceType;

typedef enum {
    SCHED_HRRN = 0,        /* Highest Response Ratio Next (non-preemptive)*/
    SCHED_RR   = 1,        /* Round Robin (preemptive with quantum)      */
    SCHED_MLFQ = 2         /* Multi-Level Feedback Queue (4 levels)      */
} SchedulerType;

typedef enum {
    EVT_PROCESS_CREATED,
    EVT_MEMORY_ALLOCATED,
    EVT_MEMORY_FREED,
    EVT_PROCESS_READY,
    EVT_PROCESS_DISPATCHED,
    EVT_INSTRUCTION_EXECUTED,
    EVT_PROCESS_BLOCKED,
    EVT_PROCESS_UNBLOCKED,
    EVT_PROCESS_TERMINATED,
    EVT_QUANTUM_EXPIRED,
    EVT_CONTEXT_SWITCH,
    EVT_SWAP_OUT,
    EVT_SWAP_IN,
    EVT_CPU_IDLE,
    EVT_DEADLOCK,
    EVT_SIMULATION_COMPLETED,
    EVT_MLFQ_DEMOTE,
    EVT_PRINT_OUTPUT,
    EVT_FILE_WRITE,
    EVT_FILE_READ,
    EVT_ERROR
} EventType;

/* ========================================================================
   Simulation Event
   ======================================================================== */
typedef struct {
    int time;
    EventType type;
    int pid;
    char message[256];
    char detail[512];
} SimEvent;

/* ========================================================================
   Memory Word
   ======================================================================== */
typedef struct {
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} MemoryWord;

/* ========================================================================
   Process Control Block (PCB)
   ======================================================================== */
typedef struct PCB {
    int pid;
    State state;
    int program_counter;
    int mem_start;
    int mem_end;
    bool in_memory;
    void *swap_data;                      /* saved variable state during swap-out */
    int waiting_time;
    int burst_time;
    int remaining_time;
    int priority_level;
    char program_name[MAX_FILENAME_LEN];
    char **instructions;
    int num_instructions;
    int arrival_time;
    int completion_time;                  /* time step when finished, -1 if not */
    int first_run_time;                   /* first time dispatched, -1 if not   */
    int turnaround_time;
    int response_time;
    struct PCB *next;
} PCB;

/* ========================================================================
   Mutex Structure
   ======================================================================== */
typedef struct {
    int locked;                  /* 1 = locked, 0 = free               */
    int owner_pid;               /* PID of current owner, -1 if none   */
} Mutex;

/* ========================================================================
   Input Map (for batch / API simulation mode)
   ======================================================================== */
typedef struct {
    char var_name[MAX_VAR_NAME];
    char var_value[MAX_VAR_VALUE];
} InputMapping;

typedef struct {
    int pid;
    InputMapping entries[MAX_INPUT_VARS];
    int count;
} InputMap;

typedef struct {
    int pid;
    char file[MAX_FILENAME_LEN];
    int arrival_time;
    InputMap input_map;
} ProcessSpec;

/* ========================================================================
   Simulation Configuration
   ======================================================================== */
typedef struct {
    SchedulerType algorithm;
    int rr_quantum;              /* default 2                          */
    char programs_dir[256];
    char sandbox_dir[256];
    int process_count;
    ProcessSpec processes[MAX_PROCESSES];
    int max_steps;               /* safety limit, default 5000         */
} SimulationConfig;

/* ========================================================================
   Simulation Metrics
   ======================================================================== */
typedef struct {
    double avg_waiting_time;
    double avg_turnaround_time;
    double avg_response_time;
    double cpu_utilization;
    double throughput;
    int context_switches;
    int total_time;
    int completed_processes;
} SimulationMetrics;

/* ========================================================================
   Forward Declarations
   ======================================================================== */
typedef struct Scheduler Scheduler;
typedef struct Simulation Simulation;

/* ========================================================================
   Memory Management API
   ======================================================================== */
void init_memory(MemoryWord *mem, int *owners);
bool allocate_memory_block(PCB *p, MemoryWord *mem, int *owners);
void free_memory_block(PCB *p, MemoryWord *mem, int *owners);
void pcb_flush_to_memory(PCB *p, MemoryWord *mem);
bool load_process_into_memory(PCB *p, MemoryWord *mem, int *owners);
bool load_instruction(PCB *p, int pc, char *out_buffer, int max_size);
bool store_variable(PCB *p, MemoryWord *mem, const char *var_name, const char *value);
char *load_variable(PCB *p, MemoryWord *mem, const char *var_name);
bool swap_out(PCB *p, MemoryWord *mem, int *owners);
bool swap_in(PCB *p, MemoryWord *mem, int *owners);
void print_memory(MemoryWord *mem, int *owners);

/* ========================================================================
   Process Management API
   ======================================================================== */
PCB *create_process(int pid, const char *program_filename, int arrival_time);
void destroy_process(PCB *p);

/* ========================================================================
   Scheduler API
   ======================================================================== */
Scheduler *scheduler_create(SchedulerType algo);
void scheduler_destroy(Scheduler *s);
void scheduler_add_to_ready(Scheduler *s, PCB *p, MemoryWord *mem);
void scheduler_add_to_blocked(Scheduler *s, ResourceType res, PCB *p, MemoryWord *mem);
PCB *scheduler_unblock_one(Scheduler *s, ResourceType res, MemoryWord *mem);
PCB *scheduler_get_next(Scheduler *s);
void scheduler_on_tick(Scheduler *s, PCB *running);
SchedulerType scheduler_get_type(Scheduler *s);
bool scheduler_has_ready(Scheduler *s);
bool scheduler_has_blocked(Scheduler *s);
void scheduler_print_queues(Scheduler *s);

/* Inspection helpers for snapshot serialization */
int scheduler_get_ready_pids(Scheduler *s, int *out_pids, int max);
int scheduler_get_mlfq_pids(Scheduler *s, int level, int *out_pids, int max);
int scheduler_get_blocked_pids(Scheduler *s, ResourceType res, int *out_pids, int max);

/* ========================================================================
   Mutex Synchronization API
   ======================================================================== */
void init_mutexes(Mutex *mutexes);
bool sim_sem_wait(Mutex *mutexes, Scheduler *scheduler, ResourceType res,
                  PCB *p, MemoryWord *mem);
void sim_sem_signal(Mutex *mutexes, Scheduler *scheduler, ResourceType res,
                    PCB *p, MemoryWord *mem);

/* ========================================================================
   Execution Context & Interpreter API
   ======================================================================== */
typedef struct {
    Simulation *sim;
    MemoryWord *mem;
    int *mem_owner;
    Mutex *mutexes;
    Scheduler *scheduler;
    InputMap *input_map;
    char sandbox_dir[256];
} ExecContext;

void execute_instruction(PCB *p, ExecContext *ctx);

/* ========================================================================
   Simulation Engine API
   ======================================================================== */
Simulation *simulation_create(const SimulationConfig *config);
int  simulation_step(Simulation *sim);
void simulation_run(Simulation *sim);
void simulation_destroy(Simulation *sim);
void simulation_write_json(Simulation *sim, FILE *out);

/* Event & diagnostic helpers */
void simulation_add_event(Simulation *sim, int time, EventType type,
                          int pid, const char *msg, const char *detail);
void simulation_add_output(Simulation *sim, int pid, const char *text);
void simulation_add_error(Simulation *sim, const char *error_msg);
void simulation_add_file_touched(Simulation *sim, const char *filename);
int  simulation_get_time(Simulation *sim);

/* ========================================================================
   String Representation Helpers
   ======================================================================== */
const char *state_name(State s);
const char *event_type_name(EventType t);
const char *scheduler_name(SchedulerType t);
const char *resource_name(ResourceType r);

#endif /* OS_SHARED_H */
