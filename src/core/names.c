/* src/core/names.c – Human-readable strings for enums */
#include "os_shared.h"

const char *state_name(State s)
{
    switch (s) {
    case NEW:      return "NEW";
    case READY:    return "READY";
    case RUNNING:  return "RUNNING";
    case BLOCKED:  return "BLOCKED";
    case FINISHED: return "FINISHED";
    default:       return "UNKNOWN";
    }
}

const char *event_type_name(EventType t)
{
    switch (t) {
    case EVT_PROCESS_CREATED:       return "PROCESS_CREATED";
    case EVT_MEMORY_ALLOCATED:      return "MEMORY_ALLOCATED";
    case EVT_MEMORY_FREED:          return "MEMORY_FREED";
    case EVT_PROCESS_READY:         return "PROCESS_READY";
    case EVT_PROCESS_DISPATCHED:    return "PROCESS_DISPATCHED";
    case EVT_INSTRUCTION_EXECUTED:  return "INSTRUCTION_EXECUTED";
    case EVT_PROCESS_BLOCKED:       return "PROCESS_BLOCKED";
    case EVT_PROCESS_UNBLOCKED:     return "PROCESS_UNBLOCKED";
    case EVT_PROCESS_TERMINATED:    return "PROCESS_TERMINATED";
    case EVT_QUANTUM_EXPIRED:       return "QUANTUM_EXPIRED";
    case EVT_CONTEXT_SWITCH:        return "CONTEXT_SWITCH";
    case EVT_SWAP_OUT:              return "SWAP_OUT";
    case EVT_SWAP_IN:               return "SWAP_IN";
    case EVT_CPU_IDLE:              return "CPU_IDLE";
    case EVT_DEADLOCK:              return "DEADLOCK";
    case EVT_SIMULATION_COMPLETED:  return "SIMULATION_COMPLETED";
    case EVT_MLFQ_DEMOTE:          return "MLFQ_DEMOTE";
    case EVT_PRINT_OUTPUT:         return "PRINT_OUTPUT";
    case EVT_FILE_WRITE:           return "FILE_WRITE";
    case EVT_FILE_READ:            return "FILE_READ";
    case EVT_ERROR:                return "ERROR";
    default:                        return "UNKNOWN_EVENT";
    }
}

const char *scheduler_name(SchedulerType t)
{
    switch (t) {
    case SCHED_HRRN: return "HRRN";
    case SCHED_RR:   return "RR";
    case SCHED_MLFQ: return "MLFQ";
    default:         return "UNKNOWN";
    }
}

const char *resource_name(ResourceType r)
{
    switch (r) {
    case RES_USER_INPUT:  return "userInput";
    case RES_USER_OUTPUT: return "userOutput";
    case RES_FILE:        return "file";
    default:              return "unknown";
    }
}

