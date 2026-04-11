#include "queue.h"

void init_queue(PCBQueue *q)
{
    q->head = NULL;
    q->tail = NULL;
}

bool is_queue_empty(PCBQueue *q)
{
    return q->head == NULL;
}

void enqueue(PCBQueue *q, PCB *p)
{
    if (!p)
        return;

    p->next = NULL;
    if (q->tail)
    {
        q->tail->next = p;
        q->tail = p;
    }
    else
    {
        q->head = q->tail = p;
    }
}

PCB *dequeue(PCBQueue *q)
{
    if (!q->head)
        return NULL;

    PCB *p = q->head;
    q->head = p->next;
    if (!q->head)
        q->tail = NULL;

    p->next = NULL;
    return p;
}

PCB *remove_from_queue(PCBQueue *q, PCB *p)
{
    if (!p || !q->head)
        return NULL;

    PCB *prev = NULL;
    PCB *cur = q->head;

    while (cur)
    {
        if (cur == p)
        {
            if (prev)
                prev->next = cur->next;
            else
                q->head = cur->next;

            if (q->tail == cur)
                q->tail = prev;

            cur->next = NULL;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }

    return NULL;
}

void print_queue(const char *name, PCBQueue *q)
{
    printf("%s: ", name);
    PCB *cur = q->head;
    if (!cur)
    {
        printf("(empty)\n");
        return;
    }
    while (cur)
    {
        printf("%d ", cur->pid);
        cur = cur->next;
    }
    printf("\n");
}
