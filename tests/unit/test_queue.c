/* tests/unit/test_queue.c – Unit tests for PCBQueue */
#include <assert.h>
#include <stdio.h>
#include "queue.h"

int main(void)
{
    printf("Running test_queue...\n");

    PCBQueue q;
    init_queue(&q);
    assert(is_queue_empty(&q));
    assert(queue_length(&q) == 0);
    assert(dequeue(&q) == NULL);

    PCB p1, p2, p3;
    memset(&p1, 0, sizeof(p1)); p1.pid = 1;
    memset(&p2, 0, sizeof(p2)); p2.pid = 2;
    memset(&p3, 0, sizeof(p3)); p3.pid = 3;

    /* Enqueue and order */
    enqueue(&q, &p1);
    assert(!is_queue_empty(&q));
    assert(queue_length(&q) == 1);

    enqueue(&q, &p2);
    enqueue(&q, &p3);
    assert(queue_length(&q) == 3);

    int pids[5];
    int count = queue_to_array(&q, pids, 5);
    assert(count == 3);
    assert(pids[0] == 1 && pids[1] == 2 && pids[2] == 3);

    /* Remove middle element */
    PCB *rem = remove_from_queue(&q, &p2);
    assert(rem == &p2);
    assert(queue_length(&q) == 2);

    /* Dequeue remaining */
    PCB *first = dequeue(&q);
    assert(first == &p1);
    PCB *second = dequeue(&q);
    assert(second == &p3);
    assert(is_queue_empty(&q));

    printf("✓ test_queue passed successfully!\n");
    return 0;
}

