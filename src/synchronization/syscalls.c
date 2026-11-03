/* src/synchronization/syscalls.c – Mutex and semaphore operations */
#include "os_shared.h"

void init_mutexes(Mutex *mutexes)
{
    if (!mutexes) return;
    for (int i = 0; i < RES_COUNT; ++i) {
        mutexes[i].locked = 0;
        mutexes[i].owner_pid = -1;
    }
}

bool sim_sem_wait(Mutex *mutexes, Scheduler *scheduler, ResourceType res,
                  PCB *p, MemoryWord *mem)
{
    if (!mutexes || !scheduler || !p || (int)res < 0 || (int)res >= RES_COUNT)
        return false;

    Mutex *mutex = &mutexes[res];
    if (!mutex->locked) {
        mutex->locked = 1;
        mutex->owner_pid = p->pid;
        return true;
    }

    /* Mutex is already locked; block this process */
    scheduler_add_to_blocked(scheduler, res, p, mem);
    return false;
}

void sim_sem_signal(Mutex *mutexes, Scheduler *scheduler, ResourceType res,
                    PCB *p, MemoryWord *mem)
{
    if (!mutexes || !scheduler || (int)res < 0 || (int)res >= RES_COUNT)
        return;

    Mutex *mutex = &mutexes[res];
    /* Only owner can release (if locked) */
    if (mutex->locked && p && mutex->owner_pid != p->pid) {
        return;
    }

    PCB *unblocked = scheduler_unblock_one(scheduler, res, mem);
    if (unblocked) {
        /* Direct hand-off to unblocked process */
        mutex->locked = 1;
        mutex->owner_pid = unblocked->pid;
    } else {
        mutex->locked = 0;
        mutex->owner_pid = -1;
    }
}

