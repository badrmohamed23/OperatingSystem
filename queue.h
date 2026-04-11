#ifndef QUEUE_H
#define QUEUE_H

#include "os_shared.h"

// Generic linked-list queue of PCBs using PCB->next

typedef struct
{
    PCB *head;
    PCB *tail;
} PCBQueue;

void init_queue(PCBQueue *q);
bool is_queue_empty(PCBQueue *q);
void enqueue(PCBQueue *q, PCB *p);
PCB *dequeue(PCBQueue *q);
PCB *remove_from_queue(PCBQueue *q, PCB *p);
void print_queue(const char *name, PCBQueue *q);

#endif // QUEUE_H
