#include "os_shared.h"
#include "queue.h"

static PCBQueue ready_queue;
static PCBQueue blocked_queues[3]; // indexed by ResourceType
static SchedulerType current_scheduler = SCHED_HRRN;
static int current_time = 0; // counts executed instructions

static PCBQueue *get_blocked_queue(ResourceType res)
{
    switch (res)
    {
    case RES_USER_INPUT:
        return &blocked_queues[RES_USER_INPUT];
    case RES_USER_OUTPUT:
        return &blocked_queues[RES_USER_OUTPUT];
    case RES_FILE:
        return &blocked_queues[RES_FILE];
    default:
        return NULL;
    }
}

void init_scheduler(SchedulerType algo)
{
    current_scheduler = algo;
    init_queue(&ready_queue);
    for (int i = 0; i < 3; ++i)
    {
        init_queue(&blocked_queues[i]);
    }
    current_time = 0;
}

void add_to_ready(PCB *p)
{
    if (!p)
        return;
    p->state = READY;
    enqueue(&ready_queue, p);
}

void add_to_blocked(ResourceType res, PCB *p)
{
    if (!p)
        return;
    PCBQueue *q = get_blocked_queue(res);
    if (!q)
        return;
    p->state = BLOCKED;
    enqueue(q, p);
}

PCB *unblock_one(ResourceType res)
{
    PCBQueue *q = get_blocked_queue(res);
    if (!q)
        return NULL;

    PCB *p = dequeue(q);
    if (p)
    {
        p->state = READY;
        add_to_ready(p);
    }
    return p;
}

void scheduler_on_tick(PCB *running)
{
    current_time++;

    // Decrease remaining burst time for the running process
    if (running && running->burst_time > 0)
        running->burst_time--;

    // Increase waiting time for all processes in ready queue
    PCB *cur = ready_queue.head;
    while (cur)
    {
        cur->waiting_time++;
        cur = cur->next;
    }
}

static PCB *select_hrrn(void)
{
    if (is_queue_empty(&ready_queue))
        return NULL;

    PCB *best_prev = NULL;
    PCB *best = NULL;
    double best_ratio = -1.0;

    PCB *prev = NULL;
    PCB *cur = ready_queue.head;

    while (cur)
    {
        int service_time = cur->num_instructions;
        if (service_time <= 0)
            service_time = 1;

        double ratio = (double)(cur->waiting_time + service_time) / (double)service_time;
        if (ratio > best_ratio)
        {
            best_ratio = ratio;
            best = cur;
            best_prev = prev;
        }

        prev = cur;
        cur = cur->next;
    }

    // Remove best from ready_queue
    if (!best)
        return NULL;

    if (best_prev)
        best_prev->next = best->next;
    else
        ready_queue.head = best->next;

    if (ready_queue.tail == best)
        ready_queue.tail = best_prev;

    best->next = NULL;
    best->waiting_time = 0; // reset waiting time when scheduled

    return best;
}

static PCB *select_rr(void)
{
    // Simple RR: dequeue head and enqueue back if still ready later
    return dequeue(&ready_queue);
}

PCB *get_next_process(void)
{
    if (is_queue_empty(&ready_queue))
        return NULL;

    switch (current_scheduler)
    {
    case SCHED_HRRN:
        return select_hrrn();
    case SCHED_RR:
        return select_rr();
    default:
        return select_rr();
    }
}

void print_queues(void)
{
    print_queue("Ready queue", &ready_queue);
    print_queue("Blocked userInput", &blocked_queues[RES_USER_INPUT]);
    print_queue("Blocked userOutput", &blocked_queues[RES_USER_OUTPUT]);
    print_queue("Blocked file", &blocked_queues[RES_FILE]);
}
