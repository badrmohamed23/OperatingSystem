/* tests/unit/test_scheduler.c – Unit tests for HRRN, RR, and MLFQ schedulers */
#include <assert.h>
#include <stdio.h>
#include "os_shared.h"

int main(void)
{
    printf("Running test_scheduler...\n");

    /* 1. Round Robin */
    Scheduler *rr = scheduler_create(SCHED_RR);
    assert(rr != NULL);

    PCB p1, p2, p3;
    memset(&p1, 0, sizeof(p1)); p1.pid = 1; p1.burst_time = 4;
    memset(&p2, 0, sizeof(p2)); p2.pid = 2; p2.burst_time = 2;
    memset(&p3, 0, sizeof(p3)); p3.pid = 3; p3.burst_time = 6;

    scheduler_add_to_ready(rr, &p1, NULL);
    scheduler_add_to_ready(rr, &p2, NULL);
    scheduler_add_to_ready(rr, &p3, NULL);

    assert(scheduler_has_ready(rr));
    PCB *next = scheduler_get_next(rr);
    assert(next && next->pid == 1);
    next = scheduler_get_next(rr);
    assert(next && next->pid == 2);
    next = scheduler_get_next(rr);
    assert(next && next->pid == 3);
    assert(!scheduler_has_ready(rr));

    scheduler_destroy(rr);

    /* 2. HRRN: (waiting_time + service_time) / service_time */
    Scheduler *hrrn = scheduler_create(SCHED_HRRN);
    assert(hrrn != NULL);

    /* Process A: burst=10, wait=10 -> ratio = 20/10 = 2.0 */
    /* Process B: burst=2,  wait=5  -> ratio = 7/2  = 3.5 */
    PCB pa, pb;
    memset(&pa, 0, sizeof(pa)); pa.pid = 10; pa.burst_time = 10; pa.waiting_time = 10;
    memset(&pb, 0, sizeof(pb)); pb.pid = 20; pb.burst_time = 2;  pb.waiting_time = 5;

    scheduler_add_to_ready(hrrn, &pa, NULL);
    scheduler_add_to_ready(hrrn, &pb, NULL);

    /* HRRN must select PB because ratio 3.5 > 2.0 */
    PCB *hrrn_first = scheduler_get_next(hrrn);
    assert(hrrn_first && hrrn_first->pid == 20);

    PCB *hrrn_second = scheduler_get_next(hrrn);
    assert(hrrn_second && hrrn_second->pid == 10);

    scheduler_destroy(hrrn);

    /* 3. MLFQ: priority queues 0..3 */
    Scheduler *mlfq = scheduler_create(SCHED_MLFQ);
    assert(mlfq != NULL);

    PCB pm0, pm1;
    memset(&pm0, 0, sizeof(pm0)); pm0.pid = 100; pm0.priority_level = 0;
    memset(&pm1, 0, sizeof(pm1)); pm1.pid = 200; pm1.priority_level = 1;

    scheduler_add_to_ready(mlfq, &pm1, NULL);
    scheduler_add_to_ready(mlfq, &pm0, NULL);

    /* MLFQ must select PM0 first because level 0 is higher priority than level 1 */
    PCB *mlfq_first = scheduler_get_next(mlfq);
    assert(mlfq_first && mlfq_first->pid == 100);

    PCB *mlfq_second = scheduler_get_next(mlfq);
    assert(mlfq_second && mlfq_second->pid == 200);

    scheduler_destroy(mlfq);

    printf("✓ test_scheduler passed successfully!\n");
    return 0;
}

