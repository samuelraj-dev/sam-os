#ifndef TASK_H
#define TASK_H

#include "../types.h"
#include "../vmm.h"
#include "../idt.h"

#define MAX_TASKS          16
#define TASK_STACK_SIZE    16384
#define TICKS_PER_SLICE    5

typedef enum {
    TASK_NEW = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_WAITING,
    TASK_ZOMBIE,
} TaskState;

typedef struct {
    int            tid;
    int            pid;
    TaskState      state;
    uint64_t       kernel_rsp;
    uint64_t       kernel_stack_top;
    uint64_t       cr3;
    uint32_t       timeslice;
    int            exit_code;
    int            is_user;
    InterruptFrame* trapframe;
    AddressSpace*  address_space;
    uint8_t*       kernel_stack;
} Task;

void     task_init(void);
int      task_create_kernel(void (*entry)(void));
int      task_create_user(int pid, AddressSpace* as,
                          uint64_t entry, uint64_t user_rsp);
void     task_sleep(void);
void     task_wake(int tid);
void     task_yield(void);
void     task_exit(int code);
int      task_current_pid(void);
Task*    task_current(void);
void     task_print_stats(void);
int      task_kill_pid(int pid, int code);
int      task_self_check(void);
uint64_t task_get_switch_count(void);

uint64_t schedule_on_tick(uint64_t current_rsp);
uint64_t task_schedule_from_interrupt(InterruptFrame* frame);
void     schedule(void);

void shell_task(void);
void idle_task(void);

#endif
