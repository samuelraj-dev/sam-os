#include "task.h"
#include "../kmalloc.h"
#include "../display.h"

static Task     tasks[MAX_TASKS];
static int      current_task = -1;
static int      task_count   = 0;
static int      slice_ticks  = 0;
uint64_t        idle_rsp     = 0;

void task_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].rsp   = 0;
        tasks[i].stack = 0;
        tasks[i].state = TASK_UNUSED;
        tasks[i].id    = i;
    }
    current_task = -1;
    task_count   = 0;
    slice_ticks  = 0;
}

int task_create(void (*entry)(void))
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED) continue;

        uint8_t* stack = kmalloc(STACK_SIZE);
        if (!stack) return -1;
        for (int j = 0; j < STACK_SIZE; j++) stack[j] = 0;

        uint64_t* sp = (uint64_t*)((uint64_t)(stack + STACK_SIZE) & ~0xFULL);

        // iretq frame — matches what CPU pushes on ring0 interrupt
        *--sp = 0x10;               // SS
        *--sp = (uint64_t)(sp + 2); // RSP (arbitrary, task sets its own)
        *--sp = 0x202;              // RFLAGS — IF set
        *--sp = 0x08;               // CS
        *--sp = (uint64_t)entry;    // RIP

        // fake vector + error code (isr32 does addq $16 to skip these)
        *--sp = 32;  // vector
        *--sp = 0;   // error code

        // fake saved registers in isr32 push order
        // isr32 pushes: rax rbx rcx rdx rsi rdi rbp r8 r9 r10 r11 r12 r13 r14 r15
        // isr32 pops:   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
        *--sp = 0;  // rax
        *--sp = 0;  // rbx
        *--sp = 0;  // rcx
        *--sp = 0;  // rdx
        *--sp = 0;  // rsi
        *--sp = 0;  // rdi
        *--sp = 0;  // rbp
        *--sp = 0;  // r8
        *--sp = 0;  // r9
        *--sp = 0;  // r10
        *--sp = 0;  // r11
        *--sp = 0;  // r12
        *--sp = 0;  // r13
        *--sp = 0;  // r14
        *--sp = 0;  // r15  ← RSP points here

        tasks[i].rsp   = (uint64_t)sp;
        tasks[i].stack = stack;
        tasks[i].state = TASK_READY;
        task_count++;
        return i;
    }
    return -1;
}

static int find_next(void)
{
    if (task_count == 0) return -1;

    int start = (current_task < 0) ? 0 : (current_task + 1) % MAX_TASKS;

    for (int i = 0; i < MAX_TASKS; i++) {
        int idx = (start + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_READY ||
            tasks[idx].state == TASK_RUNNING)
            return idx;
    }
    return -1;
}

// called from isr32 — saves current RSP, returns new RSP or 0
uint64_t schedule_on_tick(uint64_t current_rsp)
{
    if (task_count == 0) return 0;

    slice_ticks++;
    if (slice_ticks < TICKS_PER_SLICE) return 0;
    slice_ticks = 0;

    // save current task RSP
    if (current_task >= 0)
        tasks[current_task].rsp = current_rsp;
    else
        idle_rsp = current_rsp;

    int next = find_next();
    if (next < 0 || next == current_task) return 0;

    if (current_task >= 0)
        tasks[current_task].state = TASK_READY;

    current_task = next;
    tasks[next].state = TASK_RUNNING;

    return tasks[next].rsp;
}

// enters task system from kernel — does not return
void schedule(void)
{
    int next = find_next();
    if (next < 0) return;

    current_task = next;
    tasks[next].state = TASK_RUNNING;

    __asm__ volatile (
        "movq %0, %%rsp\n"
        "popq %%r15\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%r11\n"
        "popq %%r10\n"
        "popq %%r9\n"
        "popq %%r8\n"
        "popq %%rbp\n"
        "popq %%rdi\n"
        "popq %%rsi\n"
        "popq %%rdx\n"
        "popq %%rcx\n"
        "popq %%rbx\n"
        "popq %%rax\n"
        "addq $16, %%rsp\n"
        "iretq\n"
        :
        : "r"(tasks[next].rsp)
        : "memory"
    );
}

void task1(void)
{
    while (1) {
        print("A");
        display_flush();
        __asm__ volatile ("hlt");
    }
}

void task2(void)
{
    while (1) {
        print("B");
        display_flush();
        __asm__ volatile ("hlt");
    }
}