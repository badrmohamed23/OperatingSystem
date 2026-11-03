/* src/scheduler/scheduler.c – HRRN / Round Robin / MLFQ scheduling */
#include "os_shared.h"
#include "queue.h"

struct Scheduler {
    SchedulerType algorithm;
    PCBQueue      ready_queue;                /* for HRRN and RR         */
    PCBQueue      mlfq_queues[4];             /* 4 MLFQ priority levels  */
    PCBQueue      blocked_queues[RES_COUNT];  /* per resource            */
};

Scheduler *scheduler_create(SchedulerType algo)
{
    Scheduler *s = (Scheduler *)calloc(1, sizeof(Scheduler));
    if (!s) return NULL;
    s->algorithm = algo;
    init_queue(&s->ready_queue);
    for (int i = 0; i < 4; ++i) init_queue(&s->mlfq_queues[i]);
    for (int i = 0; i < RES_COUNT; ++i) init_queue(&s->blocked_queues[i]);
    return s;
}

void scheduler_destroy(Scheduler *s)
{
    if (s) free(s);
}

void scheduler_add_to_ready(Scheduler *s, PCB *p, MemoryWord *mem)
{
    if (!s || !p) return;
    p->state = READY;
    if (p->in_memory && mem) pcb_flush_to_memory(p, mem);

    if (s->algorithm == SCHED_MLFQ) {
        int level = p->priority_level;
        if (level < 0) level = 0;
        if (level > 3) level = 3;
        p->priority_level = level;
        enqueue(&s->mlfq_queues[level], p);
    } else {
        enqueue(&s->ready_queue, p);
    }
}

void scheduler_add_to_blocked(Scheduler *s, ResourceType res, PCB *p, MemoryWord *mem)
{
    if (!s || !p || (int)res < 0 || (int)res >= RES_COUNT) return;
    p->state = BLOCKED;
    if (p->in_memory && mem) pcb_flush_to_memory(p, mem);
    enqueue(&s->blocked_queues[(int)res], p);
}

PCB *scheduler_unblock_one(Scheduler *s, ResourceType res, MemoryWord *mem)
{
    if (!s || (int)res < 0 || (int)res >= RES_COUNT) return NULL;
    PCB *p = dequeue(&s->blocked_queues[(int)res]);
    if (p) {
        p->state = READY;
        if (p->in_memory && mem) pcb_flush_to_memory(p, mem);
        scheduler_add_to_ready(s, p, mem);
    }
    return p;
}

/* HRRN: (waiting_time + service_time) / service_time */
static PCB *select_hrrn(Scheduler *s)
{
    if (is_queue_empty(&s->ready_queue)) return NULL;

    PCB *best_prev = NULL;
    PCB *best = NULL;
    double best_ratio = -1.0;

    PCB *prev = NULL;
    PCB *cur = s->ready_queue.head;

    while (cur) {
        int service_time = cur->burst_time > 0 ? cur->burst_time : 1;
        double ratio = (double)(cur->waiting_time + service_time) / (double)service_time;

        if (ratio > best_ratio || (ratio == best_ratio && best && cur->pid < best->pid)) {
            best_ratio = ratio;
            best       = cur;
            best_prev  = prev;
        }

        prev = cur;
        cur  = cur->next;
    }

    if (!best) return NULL;

    if (best_prev) best_prev->next = best->next;
    else           s->ready_queue.head = best->next;
    if (s->ready_queue.tail == best) s->ready_queue.tail = best_prev;
    best->next = NULL;

    return best;
}

static PCB *select_rr(Scheduler *s)
{
    return dequeue(&s->ready_queue);
}

static PCB *select_mlfq(Scheduler *s)
{
    for (int level = 0; level < 4; ++level) {
        if (!is_queue_empty(&s->mlfq_queues[level])) {
            PCB *p = dequeue(&s->mlfq_queues[level]);
            if (p) {
                p->priority_level = level;
                return p;
            }
        }
    }
    return NULL;
}

PCB *scheduler_get_next(Scheduler *s)
{
    if (!s) return NULL;
    switch (s->algorithm) {
    case SCHED_HRRN: return select_hrrn(s);
    case SCHED_RR:   return select_rr(s);
    case SCHED_MLFQ: return select_mlfq(s);
    default:         return select_rr(s);
    }
}

void scheduler_on_tick(Scheduler *s, PCB *running)
{
    if (!s) return;

    if (running && running->remaining_time > 0)
        running->remaining_time--;

    /* Every process currently waiting in ready queues increments waiting_time */
    if (s->algorithm == SCHED_MLFQ) {
        for (int level = 0; level < 4; ++level) {
            PCB *cur = s->mlfq_queues[level].head;
            while (cur) {
                cur->waiting_time++;
                cur = cur->next;
            }
        }
    } else {
        PCB *cur = s->ready_queue.head;
        while (cur) {
            cur->waiting_time++;
            cur = cur->next;
        }
    }
}

SchedulerType scheduler_get_type(Scheduler *s)
{
    return s ? s->algorithm : SCHED_RR;
}

bool scheduler_has_blocked(Scheduler *s)
{
    if (!s) return false;
    for (int i = 0; i < RES_COUNT; ++i) {
        if (!is_queue_empty(&s->blocked_queues[i])) return true;
    }
    return false;
}

bool scheduler_has_ready(Scheduler *s)
{
    if (!s) return false;
    if (s->algorithm == SCHED_MLFQ) {
        for (int level = 0; level < 4; ++level) {
            if (!is_queue_empty(&s->mlfq_queues[level])) return true;
        }
        return false;
    }
    return !is_queue_empty(&s->ready_queue);
}

int scheduler_get_ready_pids(Scheduler *s, int *out_pids, int max)
{
    if (!s || !out_pids || max <= 0) return 0;
    return queue_to_array(&s->ready_queue, out_pids, max);
}

int scheduler_get_mlfq_pids(Scheduler *s, int level, int *out_pids, int max)
{
    if (!s || !out_pids || max <= 0 || level < 0 || level >= 4) return 0;
    return queue_to_array(&s->mlfq_queues[level], out_pids, max);
}

int scheduler_get_blocked_pids(Scheduler *s, ResourceType res, int *out_pids, int max)
{
    if (!s || !out_pids || max <= 0 || (int)res < 0 || (int)res >= RES_COUNT) return 0;
    return queue_to_array(&s->blocked_queues[(int)res], out_pids, max);
}

void scheduler_print_queues(Scheduler *s)
{
    if (!s) return;
    if (s->algorithm == SCHED_MLFQ) {
        for (int i = 0; i < 4; ++i) {
            char name[32];
            snprintf(name, sizeof(name), "Ready Q%d", i);
            print_queue(name, &s->mlfq_queues[i]);
        }
    } else {
        print_queue("Ready queue", &s->ready_queue);
    }
    print_queue("Blocked userInput",  &s->blocked_queues[RES_USER_INPUT]);
    print_queue("Blocked userOutput", &s->blocked_queues[RES_USER_OUTPUT]);
    print_queue("Blocked file",       &s->blocked_queues[RES_FILE]);
}

