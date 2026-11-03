/* src/queue/queue.c – Generic linked-list queue of PCBs */
#include "queue.h"

void init_queue(PCBQueue *q)
{
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
}

bool is_queue_empty(PCBQueue *q)
{
    return (!q || q->head == NULL);
}

void enqueue(PCBQueue *q, PCB *p)
{
    if (!q || !p) return;
    p->next = NULL;
    if (q->tail) {
        q->tail->next = p;
        q->tail = p;
    } else {
        q->head = q->tail = p;
    }
}

PCB *dequeue(PCBQueue *q)
{
    if (!q || !q->head) return NULL;
    PCB *p = q->head;
    q->head = p->next;
    if (!q->head) q->tail = NULL;
    p->next = NULL;
    return p;
}

PCB *remove_from_queue(PCBQueue *q, PCB *p)
{
    if (!q || !p || !q->head) return NULL;
    PCB *prev = NULL;
    PCB *cur = q->head;
    while (cur) {
        if (cur == p) {
            if (prev) prev->next = cur->next;
            else      q->head = cur->next;
            if (q->tail == cur) q->tail = prev;
            cur->next = NULL;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    return NULL;
}

int queue_length(PCBQueue *q)
{
    if (!q) return 0;
    int len = 0;
    PCB *cur = q->head;
    while (cur) {
        len++;
        cur = cur->next;
    }
    return len;
}

int queue_to_array(PCBQueue *q, int *out_pids, int max)
{
    if (!q || !out_pids || max <= 0) return 0;
    int count = 0;
    PCB *cur = q->head;
    while (cur && count < max) {
        out_pids[count++] = cur->pid;
        cur = cur->next;
    }
    return count;
}

void print_queue(const char *name, PCBQueue *q)
{
    fprintf(stderr, "%s: ", name ? name : "Queue");
    if (!q || !q->head) {
        fprintf(stderr, "(empty)\n");
        return;
    }
    PCB *cur = q->head;
    while (cur) {
        fprintf(stderr, "%d ", cur->pid);
        cur = cur->next;
    }
    fprintf(stderr, "\n");
}

