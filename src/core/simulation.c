/* src/core/simulation.c – Single authoritative C simulation engine */
#include "os_shared.h"

#define MAX_SNAPSHOTS 2048
#define MAX_OUTPUTS   1024
#define MAX_ERRORS    128
#define MAX_FILES     64

typedef struct {
    int index;
    int owner_pid;              /* -1 if free */
    char kind[8];               /* "FREE", "PCB", "VAR", "INSTR" */
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} SnapshotSlot;

typedef struct {
    int pid;
    char state[16];
    int start;
    int end;
    int required_words;
    int allocated_words;
} SnapshotSegment;

typedef struct {
    int pid;
    char file[MAX_FILENAME_LEN];
    char state[16];
    int pc;
    int total_instructions;
    int waiting_time;
    int remaining_time;
    int priority_level;
    int arrival_time;
    int completion_time;
    int response_time;
    int turnaround_time;
    int var_count;
    MemoryWord variables[PROCESS_VAR_SLOTS];
} SnapshotProcess;

typedef struct {
    int step;
    int time;
    char label[128];
    int ready_queue[MAX_PROCESSES];
    int ready_count;
    int mlfq_queues[4][MAX_PROCESSES];
    int mlfq_counts[4];
    int blocked_queues[RES_COUNT][MAX_PROCESSES];
    int blocked_counts[RES_COUNT];
    SnapshotProcess processes[MAX_PROCESSES];
    int process_count;
    struct {
        bool locked;
        int owner_pid;          /* -1 if none */
    } mutexes[RES_COUNT];
    struct {
        int running_pid;        /* -1 if none */
        int quantum;
        int executed;
        char instruction[MAX_INSTRUCTION_LEN];
        char scheduler_decision[128];
    } dispatch;
    struct {
        int size;
        int used_words;
        int free_words;
        SnapshotSegment segments[MAX_PROCESSES];
        int segment_count;
        SnapshotSlot slots[MEMORY_SIZE];
    } memory;
} Snapshot;

typedef struct {
    int step;
    int time;
    int pid;
    char text[MAX_VAR_VALUE];
} SimOutput;

struct Simulation {
    SimulationConfig config;
    MemoryWord memory[MEMORY_SIZE];
    int owners[MEMORY_SIZE];
    Mutex mutexes[RES_COUNT];
    Scheduler *scheduler;
    PCB *processes[MAX_PROCESSES];
    int count;

    int time;
    int step;
    int idle_steps;
    int last_running_pid;
    int context_switches;
    int busy_cpu_ticks;
    bool complete;

    SimEvent events[MAX_EVENTS];
    int event_count;

    Snapshot *snapshots;
    int snapshot_count;
    int snapshot_capacity;

    SimOutput outputs[MAX_OUTPUTS];
    int output_count;

    char errors[MAX_ERRORS][256];
    int error_count;

    char files_touched[MAX_FILES][MAX_FILENAME_LEN];
    int files_touched_count;
};

/* --------------------------------------------------------------------
   Event and logging helpers
   -------------------------------------------------------------------- */
static void add_event(Simulation *sim, EventType type, int pid,
                      const char *message, const char *detail)
{
    if (!sim || sim->event_count >= MAX_EVENTS) return;
    SimEvent *event = &sim->events[sim->event_count++];
    event->time = sim->time;
    event->type = type;
    event->pid = pid;
    snprintf(event->message, sizeof(event->message), "%s", message ? message : "");
    snprintf(event->detail, sizeof(event->detail), "%s", detail ? detail : "");
}

void simulation_add_event(Simulation *sim, int time, EventType type,
                          int pid, const char *msg, const char *detail)
{
    if (!sim) return;
    sim->time = time;
    add_event(sim, type, pid, msg, detail);
}

void simulation_add_output(Simulation *sim, int pid, const char *text)
{
    if (!sim || !text || sim->output_count >= MAX_OUTPUTS) return;
    SimOutput *out = &sim->outputs[sim->output_count++];
    out->step = sim->step;
    out->time = sim->time;
    out->pid  = pid;
    snprintf(out->text, sizeof(out->text), "%s", text);
}

void simulation_add_error(Simulation *sim, const char *error_msg)
{
    if (!sim || !error_msg || sim->error_count >= MAX_ERRORS) return;
    snprintf(sim->errors[sim->error_count++], 256, "%s", error_msg);
    add_event(sim, EVT_ERROR, -1, error_msg, "");
}

void simulation_add_file_touched(Simulation *sim, const char *filename)
{
    if (!sim || !filename || sim->files_touched_count >= MAX_FILES) return;
    for (int i = 0; i < sim->files_touched_count; ++i) {
        if (strcmp(sim->files_touched[i], filename) == 0) return;
    }
    snprintf(sim->files_touched[sim->files_touched_count++], MAX_FILENAME_LEN, "%s", filename);
}

int simulation_get_time(Simulation *sim) { return sim ? sim->time : 0; }

/* --------------------------------------------------------------------
   Snapshot recording
   -------------------------------------------------------------------- */
static void take_snapshot(Simulation *sim, const char *label, int running_pid,
                          int quantum, int executed, const char *current_instruction,
                          const char *decision)
{
    if (!sim) return;
    if (sim->snapshot_count >= sim->snapshot_capacity) {
        int new_cap = sim->snapshot_capacity == 0 ? 128 : sim->snapshot_capacity * 2;
        if (new_cap > MAX_SNAPSHOTS) new_cap = MAX_SNAPSHOTS;
        if (sim->snapshot_count >= new_cap) return;
        Snapshot *new_snaps = (Snapshot *)realloc(sim->snapshots, sizeof(Snapshot) * (size_t)new_cap);
        if (!new_snaps) return;
        sim->snapshots = new_snaps;
        sim->snapshot_capacity = new_cap;
    }

    Snapshot *snap = &sim->snapshots[sim->snapshot_count++];
    memset(snap, 0, sizeof(*snap));
    snap->step = sim->step;
    snap->time = sim->time;
    snprintf(snap->label, sizeof(snap->label), "%s", label ? label : "");

    /* Queues */
    snap->ready_count = scheduler_get_ready_pids(sim->scheduler, snap->ready_queue, MAX_PROCESSES);
    for (int lvl = 0; lvl < 4; ++lvl) {
        snap->mlfq_counts[lvl] = scheduler_get_mlfq_pids(sim->scheduler, lvl, snap->mlfq_queues[lvl], MAX_PROCESSES);
    }
    for (int r = 0; r < RES_COUNT; ++r) {
        snap->blocked_counts[r] = scheduler_get_blocked_pids(sim->scheduler, (ResourceType)r,
                                                             snap->blocked_queues[r], MAX_PROCESSES);
    }

    /* Processes */
    snap->process_count = sim->count;
    for (int i = 0; i < sim->count; ++i) {
        PCB *p = sim->processes[i];
        SnapshotProcess *sp = &snap->processes[i];
        sp->pid = p->pid;
        snprintf(sp->file, sizeof(sp->file), "%s", p->program_name);
        snprintf(sp->state, sizeof(sp->state), "%s", state_name(p->state));
        sp->pc = p->program_counter;
        sp->total_instructions = p->num_instructions;
        sp->waiting_time = p->waiting_time;
        sp->remaining_time = p->remaining_time;
        sp->priority_level = p->priority_level;
        sp->arrival_time = p->arrival_time;
        sp->completion_time = p->completion_time;
        sp->response_time = p->response_time;
        sp->turnaround_time = p->turnaround_time;

        /* Extract variables */
        sp->var_count = 0;
        if (p->in_memory && p->mem_start >= 0) {
            int base = p->mem_start + PCB_WORDS;
            for (int v = 0; v < PROCESS_VAR_SLOTS; ++v) {
                int idx = base + v;
                if (idx < MEMORY_SIZE && sim->memory[idx].name[0] != '\0') {
                    memcpy(&sp->variables[sp->var_count++], &sim->memory[idx], sizeof(MemoryWord));
                }
            }
        }
    }

    /* Mutexes */
    for (int r = 0; r < RES_COUNT; ++r) {
        snap->mutexes[r].locked = (sim->mutexes[r].locked != 0);
        snap->mutexes[r].owner_pid = sim->mutexes[r].owner_pid;
    }

    /* Dispatch */
    snap->dispatch.running_pid = running_pid;
    snap->dispatch.quantum = quantum;
    snap->dispatch.executed = executed;
    snprintf(snap->dispatch.instruction, sizeof(snap->dispatch.instruction),
             "%s", current_instruction ? current_instruction : "");
    snprintf(snap->dispatch.scheduler_decision, sizeof(snap->dispatch.scheduler_decision),
             "%s", decision ? decision : "");

    /* Memory visualizer */
    snap->memory.size = MEMORY_SIZE;
    int used = 0;
    for (int i = 0; i < MEMORY_SIZE; ++i) {
        SnapshotSlot *slot = &snap->memory.slots[i];
        slot->index = i;
        slot->owner_pid = sim->owners[i];
        if (sim->owners[i] == -1) {
            strncpy(slot->kind, "FREE", sizeof(slot->kind));
            strncpy(slot->name, "-", sizeof(slot->name));
            strncpy(slot->value, "-", sizeof(slot->value));
        } else {
            used++;
            PCB *owner_pcb = NULL;
            for (int p_idx = 0; p_idx < sim->count; ++p_idx) {
                if (sim->processes[p_idx]->pid == sim->owners[i]) {
                    owner_pcb = sim->processes[p_idx];
                    break;
                }
            }
            if (owner_pcb && i < owner_pcb->mem_start + PCB_WORDS) {
                strncpy(slot->kind, "PCB", sizeof(slot->kind));
            } else if (owner_pcb && i < owner_pcb->mem_start + PCB_WORDS + PROCESS_VAR_SLOTS) {
                strncpy(slot->kind, "VAR", sizeof(slot->kind));
            } else {
                strncpy(slot->kind, "INSTR", sizeof(slot->kind));
            }
            strncpy(slot->name, sim->memory[i].name[0] ? sim->memory[i].name : "-", sizeof(slot->name));
            strncpy(slot->value, sim->memory[i].value[0] ? sim->memory[i].value : "-", sizeof(slot->value));
        }
    }
    snap->memory.used_words = used;
    snap->memory.free_words = MEMORY_SIZE - used;

    /* Memory segments */
    snap->memory.segment_count = 0;
    for (int p_idx = 0; p_idx < sim->count; ++p_idx) {
        PCB *p = sim->processes[p_idx];
        if (p->in_memory && p->mem_start >= 0) {
            SnapshotSegment *seg = &snap->memory.segments[snap->memory.segment_count++];
            seg->pid = p->pid;
            snprintf(seg->state, sizeof(seg->state), "%s", state_name(p->state));
            seg->start = p->mem_start;
            seg->end = p->mem_end;
            seg->required_words = PCB_WORDS + PROCESS_VAR_SLOTS + p->num_instructions;
            seg->allocated_words = p->mem_end - p->mem_start + 1;
        }
    }
}

/* --------------------------------------------------------------------
   Simulation lifecycle
   -------------------------------------------------------------------- */
Simulation *simulation_create(const SimulationConfig *config)
{
    if (!config || config->process_count < 1 || config->process_count > MAX_PROCESSES)
        return NULL;

    Simulation *sim = (Simulation *)calloc(1, sizeof(*sim));
    if (!sim) return NULL;

    sim->config = *config;
    if (sim->config.algorithm != SCHED_RR && sim->config.algorithm != SCHED_MLFQ)
        sim->config.algorithm = SCHED_HRRN;
    if (sim->config.rr_quantum <= 0)
        sim->config.rr_quantum = 2;

    init_memory(sim->memory, sim->owners);
    init_mutexes(sim->mutexes);

    sim->scheduler = scheduler_create(sim->config.algorithm);
    if (!sim->scheduler) {
        free(sim);
        return NULL;
    }

    sim->count = config->process_count;
    sim->last_running_pid = -1;

    for (int i = 0; i < sim->count; ++i) {
        char filename[512];
        if (config->programs_dir[0] != '\0') {
            snprintf(filename, sizeof(filename), "%s/%s", config->programs_dir,
                     config->processes[i].file);
        } else {
            snprintf(filename, sizeof(filename), "%s", config->processes[i].file);
        }

        sim->processes[i] = create_process(config->processes[i].pid, filename,
                                           config->processes[i].arrival_time);
        if (!sim->processes[i]) {
            /* Try direct filename */
            sim->processes[i] = create_process(config->processes[i].pid,
                                               config->processes[i].file,
                                               config->processes[i].arrival_time);
        }
        if (!sim->processes[i]) {
            simulation_destroy(sim);
            return NULL;
        }
    }

    take_snapshot(sim, "Simulation initialized", -1, 0, 0, NULL, "Ready to start");
    return sim;
}

static bool all_finished(Simulation *sim)
{
    for (int i = 0; i < sim->count; ++i) {
        if (sim->processes[i]->state != FINISHED) return false;
    }
    return true;
}

static PCB *choose_swap_victim(Simulation *sim, PCB *exclude)
{
    /* 1. Prefer blocked processes that are in memory */
    for (int i = 0; i < sim->count; ++i) {
        PCB *cand = sim->processes[i];
        if (cand != exclude && cand->in_memory && cand->state == BLOCKED) {
            return cand;
        }
    }
    /* 2. Then any ready process that is in memory */
    for (int i = 0; i < sim->count; ++i) {
        PCB *cand = sim->processes[i];
        if (cand != exclude && cand->in_memory && cand->state == READY) {
            return cand;
        }
    }
    return NULL;
}

static bool ensure_in_memory(Simulation *sim, PCB *p)
{
    if (!p) return false;
    if (p->in_memory) return true;

    /* Try direct load or swap-in */
    if (p->swap_data != NULL) {
        if (swap_in(p, sim->memory, sim->owners)) {
            add_event(sim, EVT_SWAP_IN, p->pid, "Process swapped in", "");
            return true;
        }
    } else {
        if (load_process_into_memory(p, sim->memory, sim->owners)) {
            char detail[64];
            snprintf(detail, sizeof(detail), "start=%d,end=%d", p->mem_start, p->mem_end);
            add_event(sim, EVT_MEMORY_ALLOCATED, p->pid, "Memory allocated", detail);
            return true;
        }
    }

    /* Swap out victims until we can load this process */
    while (!p->in_memory) {
        PCB *victim = choose_swap_victim(sim, p);
        if (!victim) return false;

        swap_out(victim, sim->memory, sim->owners);
        add_event(sim, EVT_SWAP_OUT, victim->pid, "Process swapped out", "");

        if (p->swap_data != NULL) {
            if (swap_in(p, sim->memory, sim->owners)) {
                add_event(sim, EVT_SWAP_IN, p->pid, "Process swapped in", "");
                return true;
            }
        } else {
            if (load_process_into_memory(p, sim->memory, sim->owners)) {
                char detail[64];
                snprintf(detail, sizeof(detail), "start=%d,end=%d", p->mem_start, p->mem_end);
                add_event(sim, EVT_MEMORY_ALLOCATED, p->pid, "Memory allocated", detail);
                return true;
            }
        }
    }

    return p->in_memory;
}

static void admit_arrivals(Simulation *sim)
{
    for (int i = 0; i < sim->count; ++i) {
        PCB *p = sim->processes[i];
        if (p->state != NEW || p->arrival_time > sim->time) continue;

        ensure_in_memory(sim, p);
        scheduler_add_to_ready(sim->scheduler, p, sim->memory);

        char msg[128];
        snprintf(msg, sizeof(msg), "PID %d arrived and entered ready queue", p->pid);
        add_event(sim, EVT_PROCESS_CREATED, p->pid, msg, p->program_name);
        take_snapshot(sim, msg, -1, 0, 0, NULL, "Process arrival");
    }
}

int simulation_step(Simulation *sim)
{
    if (!sim || sim->complete) return 0;

    admit_arrivals(sim);

    if (all_finished(sim)) {
        sim->complete = true;
        add_event(sim, EVT_SIMULATION_COMPLETED, -1, "All processes completed", "");
        take_snapshot(sim, "All processes finished", -1, 0, 0, NULL, "Simulation complete");
        return 0;
    }

    PCB *running = scheduler_get_next(sim->scheduler);
    if (!running) {
        sim->idle_steps++;
        add_event(sim, EVT_CPU_IDLE, -1, "CPU idle", "No ready process");
        take_snapshot(sim, "CPU idle tick", -1, 0, 0, NULL, "Waiting for process arrival or unblock");
        sim->time++;
        sim->step++;
        if (sim->idle_steps > 2000) {
            sim->complete = true;
            add_event(sim, EVT_DEADLOCK, -1, "Simulation halted: no progress possible", "");
            take_snapshot(sim, "Execution halted", -1, 0, 0, NULL, "Possible deadlock");
        }
        return 1;
    }

    sim->idle_steps = 0;

    /* Context switch tracking */
    if (sim->last_running_pid != -1 && sim->last_running_pid != running->pid) {
        sim->context_switches++;
        char cs_msg[128];
        snprintf(cs_msg, sizeof(cs_msg), "Context switch from PID %d to PID %d",
                 sim->last_running_pid, running->pid);
        add_event(sim, EVT_CONTEXT_SWITCH, running->pid, cs_msg, "");
    }
    sim->last_running_pid = running->pid;

    /* Ensure process is resident in RAM */
    if (!ensure_in_memory(sim, running)) {
        simulation_add_error(sim, "Out of memory: unable to bring process into RAM");
        return 0;
    }

    running->state = RUNNING;
    if (running->in_memory) pcb_flush_to_memory(running, sim->memory);

    if (running->first_run_time < 0) {
        running->first_run_time = sim->time;
        running->response_time = sim->time - running->arrival_time;
    }

    /* Compute quantum */
    int quantum;
    if (sim->config.algorithm == SCHED_HRRN) {
        /* HRRN is NON-PREEMPTIVE: runs until it finishes or blocks */
        quantum = running->num_instructions - running->program_counter;
        if (quantum <= 0) quantum = 1;
    } else if (sim->config.algorithm == SCHED_RR) {
        quantum = sim->config.rr_quantum > 0 ? sim->config.rr_quantum : 2;
    } else {
        /* MLFQ: quantum = 2^priority_level */
        quantum = 1 << running->priority_level;
    }

    char dispatch_msg[128];
    snprintf(dispatch_msg, sizeof(dispatch_msg), "Scheduler dispatched PID %d (quantum=%d)",
             running->pid, quantum);
    add_event(sim, EVT_PROCESS_DISPATCHED, running->pid, dispatch_msg, "");

    /* Build execution context */
    ExecContext context;
    context.sim = sim;
    context.mem = sim->memory;
    context.mem_owner = sim->owners;
    context.mutexes = sim->mutexes;
    context.scheduler = sim->scheduler;
    context.input_map = NULL;
    for (int p_i = 0; p_i < sim->config.process_count; ++p_i) {
        if (sim->config.processes[p_i].pid == running->pid) {
            context.input_map = &sim->config.processes[p_i].input_map;
            break;
        }
    }
    snprintf(context.sandbox_dir, sizeof(context.sandbox_dir), "%s", sim->config.sandbox_dir);

    take_snapshot(sim, dispatch_msg, running->pid, quantum, 0, NULL, "Dispatch");

    int executed = 0;
    while (executed < quantum && running->state == RUNNING &&
           running->program_counter < running->num_instructions) {
        char instruction[MAX_INSTRUCTION_LEN] = {0};
        load_instruction(running, running->program_counter, instruction, sizeof(instruction));

        sim->busy_cpu_ticks++;

        char step_label[128];
        snprintf(step_label, sizeof(step_label), "PID %d executing: %s", running->pid, instruction);

        execute_instruction(running, &context);
        scheduler_on_tick(sim->scheduler, running);

        executed++;
        sim->time++;
        sim->step++;

        take_snapshot(sim, step_label, running->pid, quantum, executed, instruction, "Executing instruction");

        if (running->state == BLOCKED || running->state == FINISHED) {
            break;
        }
    }

    /* Post-execution state transitions */
    if (running->state == FINISHED) {
        running->completion_time = sim->time;
        running->turnaround_time = sim->time - running->arrival_time;
        free_memory_block(running, sim->memory, sim->owners);
        add_event(sim, EVT_MEMORY_FREED, running->pid, "Memory freed", "");
        add_event(sim, EVT_PROCESS_TERMINATED, running->pid, "Process finished", "");
        take_snapshot(sim, "Process finished", -1, 0, 0, NULL, "Termination");
    } else if (running->state == RUNNING) {
        if (sim->config.algorithm == SCHED_MLFQ && executed >= quantum) {
            /* Demote to lower priority queue */
            if (running->priority_level < 3) {
                running->priority_level++;
            }
            char demote_msg[128];
            snprintf(demote_msg, sizeof(demote_msg), "PID %d demoted to Q%d",
                     running->pid, running->priority_level);
            add_event(sim, EVT_MLFQ_DEMOTE, running->pid, demote_msg, "");
            scheduler_add_to_ready(sim->scheduler, running, sim->memory);
            take_snapshot(sim, demote_msg, -1, 0, 0, NULL, "MLFQ demotion");
        } else if (sim->config.algorithm == SCHED_RR && executed >= quantum) {
            char q_msg[128];
            snprintf(q_msg, sizeof(q_msg), "PID %d quantum expired", running->pid);
            add_event(sim, EVT_QUANTUM_EXPIRED, running->pid, q_msg, "");
            scheduler_add_to_ready(sim->scheduler, running, sim->memory);
            take_snapshot(sim, q_msg, -1, 0, 0, NULL, "RR quantum expired");
        } else {
            scheduler_add_to_ready(sim->scheduler, running, sim->memory);
        }
    }

    return 1;
}

void simulation_run(Simulation *sim)
{
    int limit = sim->config.max_steps > 0 ? sim->config.max_steps : 5000;
    while (sim && !sim->complete && sim->step < limit) {
        simulation_step(sim);
    }
}

/* --------------------------------------------------------------------
   JSON output serialization
   -------------------------------------------------------------------- */
static void json_string(FILE *out, const char *value)
{
    fputc('"', out);
    if (value) {
        for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
            if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
            else if (*p == '\n') fputs("\\n", out);
            else if (*p == '\r') fputs("\\r", out);
            else if (*p == '\t') fputs("\\t", out);
            else if (*p < 32) fprintf(out, "\\u%04x", *p);
            else fputc(*p, out);
        }
    }
    fputc('"', out);
}

void simulation_write_json(Simulation *sim, FILE *out)
{
    if (!sim || !out) return;

    /* Compute final metrics */
    int completed = 0;
    double total_wait = 0, total_tat = 0, total_resp = 0;
    for (int i = 0; i < sim->count; ++i) {
        PCB *p = sim->processes[i];
        if (p->state == FINISHED) {
            completed++;
            total_wait += p->waiting_time;
            total_tat  += p->turnaround_time;
            total_resp += p->response_time >= 0 ? p->response_time : 0;
        }
    }

    double avg_wait = completed > 0 ? total_wait / completed : 0;
    double avg_tat  = completed > 0 ? total_tat  / completed : 0;
    double avg_resp = completed > 0 ? total_resp / completed : 0;
    double cpu_util = sim->time > 0 ? ((double)sim->busy_cpu_ticks / sim->time) * 100.0 : 0;
    double throughput = sim->time > 0 ? (double)completed / sim->time : 0;

    fprintf(out, "{\n");
    fprintf(out, "  \"scheduler\": "); json_string(out, scheduler_name(sim->config.algorithm)); fprintf(out, ",\n");
    fprintf(out, "  \"rrQuantum\": %d,\n", sim->config.rr_quantum);

    /* Summary */
    fprintf(out, "  \"summary\": {\n");
    fprintf(out, "    \"totalSteps\": %d,\n", sim->step);
    fprintf(out, "    \"finalTime\": %d,\n", sim->time);
    fprintf(out, "    \"outputs\": %d,\n", sim->output_count);
    fprintf(out, "    \"errors\": %d,\n", sim->error_count);
    fprintf(out, "    \"completedProcesses\": %d,\n", completed);
    fprintf(out, "    \"filesTouched\": [");
    for (int i = 0; i < sim->files_touched_count; ++i) {
        if (i) fprintf(out, ", ");
        json_string(out, sim->files_touched[i]);
    }
    fprintf(out, "]\n");
    fprintf(out, "  },\n");

    /* Metrics */
    fprintf(out, "  \"metrics\": {\n");
    fprintf(out, "    \"avgWaitingTime\": %.2f,\n", avg_wait);
    fprintf(out, "    \"avgTurnaroundTime\": %.2f,\n", avg_tat);
    fprintf(out, "    \"avgResponseTime\": %.2f,\n", avg_resp);
    fprintf(out, "    \"cpuUtilization\": %.2f,\n", cpu_util);
    fprintf(out, "    \"throughput\": %.4f,\n", throughput);
    fprintf(out, "    \"contextSwitches\": %d,\n", sim->context_switches);
    fprintf(out, "    \"totalTime\": %d,\n", sim->time);
    fprintf(out, "    \"completedProcesses\": %d\n", completed);
    fprintf(out, "  },\n");

    /* Processes final summary */
    fprintf(out, "  \"processes\": [");
    for (int i = 0; i < sim->count; ++i) {
        if (i) fprintf(out, ",");
        PCB *p = sim->processes[i];
        fprintf(out, "\n    {\"pid\": %d, \"file\": ", p->pid);
        json_string(out, p->program_name);
        fprintf(out, ", \"state\": "); json_string(out, state_name(p->state));
        fprintf(out, ", \"pc\": %d, \"totalInstructions\": %d, \"waitingTime\": %d, \"remainingTime\": %d, "
                     "\"priorityLevel\": %d, \"arrivalTime\": %d, \"completionTime\": %d, "
                     "\"responseTime\": %d, \"turnaroundTime\": %d, \"variables\": {",
                p->program_counter, p->num_instructions, p->waiting_time, p->remaining_time,
                p->priority_level, p->arrival_time, p->completion_time,
                p->response_time, p->turnaround_time);

        int v_printed = 0;
        if (p->in_memory && p->mem_start >= 0) {
            int base = p->mem_start + PCB_WORDS;
            for (int v = 0; v < PROCESS_VAR_SLOTS; ++v) {
                int idx = base + v;
                if (idx < MEMORY_SIZE && sim->memory[idx].name[0] != '\0') {
                    if (v_printed++) fprintf(out, ", ");
                    json_string(out, sim->memory[idx].name);
                    fprintf(out, ": ");
                    json_string(out, sim->memory[idx].value);
                }
            }
        }
        fprintf(out, "}}");
    }
    fprintf(out, "\n  ],\n");

    /* Timeline events */
    fprintf(out, "  \"timeline\": [");
    for (int i = 0; i < sim->event_count; ++i) {
        if (i) fprintf(out, ",");
        SimEvent *e = &sim->events[i];
        fprintf(out, "\n    {\"id\": %d, \"step\": %d, \"time\": %d, \"type\": ",
                i + 1, i, e->time);
        json_string(out, event_type_name(e->type));
        fprintf(out, ", \"pid\": %d, \"message\": ", e->pid);
        json_string(out, e->message);
        fprintf(out, ", \"instruction\": ");
        json_string(out, e->detail);
        fprintf(out, "}");
    }
    fprintf(out, "\n  ],\n");

    /* Snapshots for scrubbing / playback */
    fprintf(out, "  \"snapshots\": [");
    for (int i = 0; i < sim->snapshot_count; ++i) {
        if (i) fprintf(out, ",");
        Snapshot *s = &sim->snapshots[i];
        fprintf(out, "\n    {\"step\": %d, \"time\": %d, \"label\": ", s->step, s->time);
        json_string(out, s->label);
        fprintf(out, ",\n     \"queues\": {\"readyQueue\": [");
        for (int q = 0; q < s->ready_count; ++q) {
            if (q) fprintf(out, ", ");
            fprintf(out, "%d", s->ready_queue[q]);
        }
        fprintf(out, "], \"mlfqQueues\": [");
        for (int lvl = 0; lvl < 4; ++lvl) {
            if (lvl) fprintf(out, ", ");
            fprintf(out, "[");
            for (int q = 0; q < s->mlfq_counts[lvl]; ++q) {
                if (q) fprintf(out, ", ");
                fprintf(out, "%d", s->mlfq_queues[lvl][q]);
            }
            fprintf(out, "]");
        }
        fprintf(out, "], \"blockedQueues\": {\"userInput\": [");
        for (int q = 0; q < s->blocked_counts[RES_USER_INPUT]; ++q) {
            if (q) fprintf(out, ", ");
            fprintf(out, "%d", s->blocked_queues[RES_USER_INPUT][q]);
        }
        fprintf(out, "], \"userOutput\": [");
        for (int q = 0; q < s->blocked_counts[RES_USER_OUTPUT]; ++q) {
            if (q) fprintf(out, ", ");
            fprintf(out, "%d", s->blocked_queues[RES_USER_OUTPUT][q]);
        }
        fprintf(out, "], \"file\": [");
        for (int q = 0; q < s->blocked_counts[RES_FILE]; ++q) {
            if (q) fprintf(out, ", ");
            fprintf(out, "%d", s->blocked_queues[RES_FILE][q]);
        }
        fprintf(out, "]}},\n");

        /* Processes in snapshot */
        fprintf(out, "     \"processes\": [");
        for (int p_idx = 0; p_idx < s->process_count; ++p_idx) {
            if (p_idx) fprintf(out, ", ");
            SnapshotProcess *sp = &s->processes[p_idx];
            fprintf(out, "{\"pid\": %d, \"file\": ", sp->pid);
            json_string(out, sp->file);
            fprintf(out, ", \"state\": "); json_string(out, sp->state);
            fprintf(out, ", \"pc\": %d, \"totalInstructions\": %d, \"waitingTime\": %d, \"remainingTime\": %d, "
                         "\"priorityLevel\": %d, \"variables\": {",
                    sp->pc, sp->total_instructions, sp->waiting_time, sp->remaining_time, sp->priority_level);
            for (int v = 0; v < sp->var_count; ++v) {
                if (v) fprintf(out, ", ");
                json_string(out, sp->variables[v].name);
                fprintf(out, ": ");
                json_string(out, sp->variables[v].value);
            }
            fprintf(out, "}}");
        }
        fprintf(out, "],\n");

        /* Mutexes */
        fprintf(out, "     \"mutexes\": {\"userInput\": {\"locked\": %s, \"ownerPid\": ",
                s->mutexes[RES_USER_INPUT].locked ? "true" : "false");
        if (s->mutexes[RES_USER_INPUT].owner_pid >= 0)
            fprintf(out, "%d", s->mutexes[RES_USER_INPUT].owner_pid);
        else fprintf(out, "null");

        fprintf(out, "}, \"userOutput\": {\"locked\": %s, \"ownerPid\": ",
                s->mutexes[RES_USER_OUTPUT].locked ? "true" : "false");
        if (s->mutexes[RES_USER_OUTPUT].owner_pid >= 0)
            fprintf(out, "%d", s->mutexes[RES_USER_OUTPUT].owner_pid);
        else fprintf(out, "null");

        fprintf(out, "}, \"file\": {\"locked\": %s, \"ownerPid\": ",
                s->mutexes[RES_FILE].locked ? "true" : "false");
        if (s->mutexes[RES_FILE].owner_pid >= 0)
            fprintf(out, "%d", s->mutexes[RES_FILE].owner_pid);
        else fprintf(out, "null");
        fprintf(out, "}},\n");

        /* Dispatch */
        fprintf(out, "     \"dispatch\": {\"runningPid\": ");
        if (s->dispatch.running_pid >= 0) fprintf(out, "%d", s->dispatch.running_pid);
        else fprintf(out, "null");
        fprintf(out, ", \"quantum\": %d, \"executed\": %d, \"instruction\": ",
                s->dispatch.quantum, s->dispatch.executed);
        if (s->dispatch.instruction[0]) json_string(out, s->dispatch.instruction);
        else fprintf(out, "null");
        fprintf(out, ", \"schedulerDecision\": "); json_string(out, s->dispatch.scheduler_decision);
        fprintf(out, "},\n");

        /* Memory visualizer in snapshot */
        fprintf(out, "     \"memory\": {\"size\": %d, \"usedWords\": %d, \"freeWords\": %d, \"segments\": [",
                s->memory.size, s->memory.used_words, s->memory.free_words);
        for (int seg_idx = 0; seg_idx < s->memory.segment_count; ++seg_idx) {
            if (seg_idx) fprintf(out, ", ");
            SnapshotSegment *seg = &s->memory.segments[seg_idx];
            fprintf(out, "{\"pid\": %d, \"state\": ", seg->pid);
            json_string(out, seg->state);
            fprintf(out, ", \"start\": %d, \"end\": %d, \"requiredWords\": %d, \"allocatedWords\": %d}",
                    seg->start, seg->end, seg->required_words, seg->allocated_words);
        }
        fprintf(out, "], \"slots\": [");
        for (int slot_idx = 0; slot_idx < MEMORY_SIZE; ++slot_idx) {
            if (slot_idx) fprintf(out, ", ");
            SnapshotSlot *slot = &s->memory.slots[slot_idx];
            fprintf(out, "{\"index\": %d, \"ownerPid\": ", slot->index);
            if (slot->owner_pid >= 0) fprintf(out, "%d", slot->owner_pid);
            else fprintf(out, "null");
            fprintf(out, ", \"kind\": "); json_string(out, slot->kind);
            fprintf(out, ", \"name\": "); json_string(out, slot->name);
            fprintf(out, ", \"value\": "); json_string(out, slot->value);
            fprintf(out, "}");
        }
        fprintf(out, "]}\n    }");
    }
    fprintf(out, "\n  ],\n");

    /* Outputs */
    fprintf(out, "  \"outputs\": [");
    for (int i = 0; i < sim->output_count; ++i) {
        if (i) fprintf(out, ",");
        SimOutput *o = &sim->outputs[i];
        fprintf(out, "\n    {\"step\": %d, \"time\": %d, \"pid\": %d, \"text\": ",
                o->step, o->time, o->pid);
        json_string(out, o->text);
        fprintf(out, "}");
    }
    fprintf(out, "\n  ],\n");

    /* Errors */
    fprintf(out, "  \"errors\": [");
    for (int i = 0; i < sim->error_count; ++i) {
        if (i) fprintf(out, ", ");
        json_string(out, sim->errors[i]);
    }
    fprintf(out, "]\n");

    fprintf(out, "}\n");
}

void simulation_destroy(Simulation *sim)
{
    if (!sim) return;
    for (int i = 0; i < sim->count; ++i) {
        destroy_process(sim->processes[i]);
    }
    if (sim->scheduler) scheduler_destroy(sim->scheduler);
    if (sim->snapshots) free(sim->snapshots);
    free(sim);
}
