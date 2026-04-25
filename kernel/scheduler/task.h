#ifndef TASK_H
#define TASK_H

#include "../types.h"

#define MAX_TASKS       8
#define STACK_SIZE      8192
#define TICKS_PER_SLICE 5

typedef enum {
    TASK_UNUSED  = 0,
    TASK_READY   = 1,
    TASK_RUNNING = 2,
} TaskState;

typedef struct {
    uint64_t  rsp;
    uint8_t*  stack;
    TaskState state;
    int       id;
} Task;

extern uint64_t idle_rsp;

void     task_init(void);
int      task_create(void (*entry)(void));
void     schedule(void);
uint64_t schedule_on_tick(uint64_t current_rsp);

void task1(void);
void task2(void);

#endif