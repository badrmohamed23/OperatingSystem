#include "os_shared.h"
#include "queue.h"

static PCBQueue ready_queue;       // used for HRRN and RR
static PCBQueue mlfq_queues[4];    // used for MLFQ: priority levels 0..3
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
    for (int i = 0; i < 4; ++i)
    {
        init_queue(&mlfq_queues[i]);
    }
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
    if (p->in_memory)
        pcb_flush_to_memory(p);

    // For MLFQ, add to the queue that corresponds to its priority level.
    if (current_scheduler == SCHED_MLFQ)
    {
        int level = p->priority_level;
        if (level < 0)
            level = 0;
        if (level > 3)
            level = 3;
        p->priority_level = level;
        enqueue(&mlfq_queues[level], p);
    }
    else
    {
        enqueue(&ready_queue, p);
    }
}

void add_to_blocked(ResourceType res, PCB *p)
{
    if (!p)
        return;
    PCBQueue *q = get_blocked_queue(res);
    if (!q)
        return;
    p->state = BLOCKED;
    if (p->in_memory)
        pcb_flush_to_memory(p);
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
        if (p->in_memory)
            pcb_flush_to_memory(p);
        add_to_ready(p);
    }
    return p;
}

void scheduler_on_tick(PCB *running)
{
    current_time++;

    // Decrease remaining burst time for the running process
    if (running && running->remaining_time > 0)
        running->remaining_time--;

    // For HRRN, increase waiting time for all processes in the HRRN ready queue
    if (current_scheduler == SCHED_HRRN)
    {
        PCB *cur = ready_queue.head;
        while (cur)
        {
            cur->waiting_time++;
            cur = cur->next;
        }
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
        // HRRN service time = total burst time stored in PCB
        int service_time = cur->burst_time;
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

static PCB *select_mlfq(void)
{
    // Select from the highest-priority non-empty queue (0 is highest)
    for (int level = 0; level < 4; ++level)
    {
        if (!is_queue_empty(&mlfq_queues[level]))
        {
            PCB *p = dequeue(&mlfq_queues[level]);
            if (p)
            {
                p->priority_level = level;
                return p;
            }
        }
    }
    return NULL;
}

PCB *get_next_process(void)
{
    switch (current_scheduler)
    {
    case SCHED_HRRN:
        if (is_queue_empty(&ready_queue))
            return NULL;
        return select_hrrn();
    case SCHED_RR:
        if (is_queue_empty(&ready_queue))
            return NULL;
        return select_rr();
    case SCHED_MLFQ:
        return select_mlfq();
    default:
        if (is_queue_empty(&ready_queue))
            return NULL;
        return select_rr();
    }
}

void print_queues(void)
{
    if (current_scheduler == SCHED_MLFQ)
    {
        print_queue("Ready Q0 (highest)", &mlfq_queues[0]);
        print_queue("Ready Q1", &mlfq_queues[1]);
        print_queue("Ready Q2", &mlfq_queues[2]);
        print_queue("Ready Q3 (lowest)", &mlfq_queues[3]);
    }
    else
    {
        print_queue("Ready queue", &ready_queue);
    }
    print_queue("Blocked userInput", &blocked_queues[RES_USER_INPUT]);
    print_queue("Blocked userOutput", &blocked_queues[RES_USER_OUTPUT]);
    print_queue("Blocked file", &blocked_queues[RES_FILE]);

    // General blocked view aggregating all resources
    printf("Blocked (all resources): ");
    bool any = false;
    for (int r = 0; r < 3; ++r)
    {
        PCB *cur = blocked_queues[r].head;
        while (cur)
        {
            printf("%d ", cur->pid);
            any = true;
            cur = cur->next;
        }
    }
    if (!any)
        printf("(none)");
    printf("\n");
}
