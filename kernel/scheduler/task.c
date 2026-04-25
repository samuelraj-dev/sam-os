#include "task.h"
#include "../kmalloc.h"
#include "../display.h"
#include "../panic.h"
#include "../klog.h"
#include "../paging.h"
extern void process_reap_deferred(void);

static Task tasks[MAX_TASKS];
static int current_tid = -1;
static uint32_t slice_ticks = 0;
static uint64_t switch_count = 0;

static const char* state_name(TaskState s)
{
    switch (s) {
        case TASK_NEW: return "NEW";
        case TASK_RUNNABLE: return "RUNNABLE";
        case TASK_RUNNING: return "RUNNING";
        case TASK_WAITING: return "WAITING";
        case TASK_ZOMBIE: return "ZOMBIE";
        default: return "?";
    }
}

static void log_transition(const char* tag, Task* t, TaskState from, TaskState to)
{
    print("[task ");
    print(tag);
    print(" tid=");
    print_dec((uint64_t)t->tid);
    print(" pid=");
    print_dec((uint64_t)t->pid);
    print(" ");
    print(state_name(from));
    print("->");
    print(state_name(to));
    print(" cr3=");
    print_hex(t->cr3);
    print("]\n");
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_ZOMBIE || tasks[i].state == TASK_NEW) {
            if (!tasks[i].kernel_stack) return i;
        }
    }

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].kernel_stack == 0 && tasks[i].state == TASK_NEW)
            return i;
    }

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_ZOMBIE) {
            if (tasks[i].kernel_stack) {
                kfree(tasks[i].kernel_stack);
                tasks[i].kernel_stack = 0;
            }
            return i;
        }
    }

    return -1;
}

static int pick_next_runnable(void)
{
    int start = (current_tid < 0) ? 0 : ((current_tid + 1) % MAX_TASKS);

    for (int i = 0; i < MAX_TASKS; i++) {
        int idx = (start + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_RUNNABLE || tasks[idx].state == TASK_RUNNING)
            return idx;
    }

    return -1;
}

static void task_switch_address_space(Task* t)
{
    kassert(t != 0, "task_switch_address_space: null task");
    if (t->is_user) {
        kassert(t->address_space != 0, "user task has null address space");
        kassert(t->cr3 != 0, "user task has null CR3");
        __asm__ volatile ("mov %0, %%cr3" :: "r"(t->cr3) : "memory");
    } else {
        kassert(kernel_address_space.pml4 != 0, "kernel address space missing");
        __asm__ volatile ("mov %0, %%cr3" :: "r"(kernel_address_space.pml4) : "memory");
    }
}

static uint64_t setup_initial_frame(Task* t, uint64_t rip, uint64_t rsp,
                                    uint64_t cs, uint64_t ss)
{
    uint64_t* top = (uint64_t*)t->kernel_stack_top;
    top -= sizeof(InterruptFrame) / sizeof(uint64_t);

    InterruptFrame* f = (InterruptFrame*)top;

    f->r15 = 0; f->r14 = 0; f->r13 = 0; f->r12 = 0;
    f->r11 = 0; f->r10 = 0; f->r9  = 0; f->r8  = 0;
    f->rbp = 0; f->rdi = 0; f->rsi = 0; f->rdx = 0;
    f->rcx = 0; f->rbx = 0; f->rax = 0;
    f->vector = 0;
    f->error_code = 0;
    f->rip = rip;
    f->cs = cs;
    f->rflags = 0x202;
    f->rsp = rsp;
    f->ss = ss;

    t->trapframe = f;
    return (uint64_t)f;
}

static uint64_t context_switch_from(uint64_t current_rsp)
{
    process_reap_deferred();

    if (current_tid >= 0) {
        Task* current = &tasks[current_tid];
        current->kernel_rsp = current_rsp;
        current->trapframe = (InterruptFrame*)current_rsp;
        if (current->state == TASK_RUNNING)
            current->state = TASK_RUNNABLE;
    }

    int next_tid = pick_next_runnable();
    if (next_tid < 0) {
        if (current_tid >= 0) {
            tasks[current_tid].state = TASK_RUNNING;
            return 0;
        }
        return 0;
    }

    if (next_tid == current_tid) {
        tasks[next_tid].state = TASK_RUNNING;
        return 0;
    }

    current_tid = next_tid;
    Task* next = &tasks[next_tid];
    next->state = TASK_RUNNING;
    next->timeslice++;

    task_switch_address_space(next);
    switch_count++;

    return next->kernel_rsp;
}

void task_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].tid = i;
        tasks[i].pid = 0;
        tasks[i].state = TASK_NEW;
        tasks[i].kernel_rsp = 0;
        tasks[i].kernel_stack_top = 0;
        tasks[i].cr3 = 0;
        tasks[i].timeslice = 0;
        tasks[i].exit_code = 0;
        tasks[i].is_user = 0;
        tasks[i].trapframe = 0;
        tasks[i].address_space = 0;
        tasks[i].kernel_stack = 0;
    }

    current_tid = -1;
    slice_ticks = 0;
    switch_count = 0;

    klog_info("task: initialized unified scheduler");
}

int task_create_kernel(void (*entry)(void))
{
    int slot = find_free_slot();
    if (slot < 0) return -1;

    uint8_t* stack = (uint8_t*)kmalloc(TASK_STACK_SIZE);
    if (!stack) return -1;

    for (int i = 0; i < TASK_STACK_SIZE; i++) stack[i] = 0;

    Task* t = &tasks[slot];
    t->pid = 0;
    t->kernel_stack = stack;
    t->kernel_stack_top = ((uint64_t)(stack + TASK_STACK_SIZE)) & ~0xFULL;
    t->cr3 = (uint64_t)kernel_address_space.pml4;
    t->is_user = 0;
    t->address_space = &kernel_address_space;
    t->exit_code = 0;
    t->timeslice = 0;

    t->kernel_rsp = setup_initial_frame(t, (uint64_t)entry,
                                        t->kernel_stack_top,
                                        0x08, 0x10);

    TaskState from = t->state;
    t->state = TASK_RUNNABLE;
    log_transition("create-k", t, from, t->state);
    return t->tid;
}

int task_create_user(int pid, AddressSpace* as, uint64_t entry, uint64_t user_rsp)
{
    int slot = find_free_slot();
    if (slot < 0) return -1;

    uint8_t* stack = (uint8_t*)kmalloc(TASK_STACK_SIZE);
    if (!stack) return -1;
    for (int i = 0; i < TASK_STACK_SIZE; i++) stack[i] = 0;

    Task* t = &tasks[slot];
    t->pid = pid;
    t->kernel_stack = stack;
    t->kernel_stack_top = ((uint64_t)(stack + TASK_STACK_SIZE)) & ~0xFULL;
    t->cr3 = (uint64_t)as->pml4;
    t->is_user = 1;
    t->address_space = as;
    t->exit_code = 0;
    t->timeslice = 0;

    t->kernel_rsp = setup_initial_frame(t, entry, user_rsp, 0x1B, 0x23);

    TaskState from = t->state;
    t->state = TASK_RUNNABLE;
    log_transition("create-u", t, from, t->state);
    return t->tid;
}

Task* task_current(void)
{
    if (current_tid < 0) return 0;
    return &tasks[current_tid];
}

int task_current_pid(void)
{
    Task* t = task_current();
    return t ? t->pid : -1;
}

void task_sleep(void)
{
    Task* t = task_current();
    kassert(t != 0, "task_sleep called without current task");
    kassert(!t->is_user, "task_sleep called from user task");

    t->state = TASK_WAITING;

    __asm__ volatile ("int $0x20");
}

void task_wake(int tid)
{
    if (tid < 0 || tid >= MAX_TASKS) return;
    Task* t = &tasks[tid];
    if (t->state == TASK_WAITING) {
        t->state = TASK_RUNNABLE;
    }
}

void task_yield(void)
{
    __asm__ volatile ("int $0x20");
}

void task_exit(int code)
{
    Task* t = task_current();
    kassert(t != 0, "task_exit called without current task");
    t->exit_code = code;
    TaskState from = t->state;
    t->state = TASK_ZOMBIE;
    log_transition("exit", t, from, t->state);
    __asm__ volatile ("int $0x20");
    while (1) __asm__ volatile ("hlt");
}

uint64_t schedule_on_tick(uint64_t current_rsp)
{
    if (current_tid >= 0 && tasks[current_tid].state == TASK_RUNNING) {
        slice_ticks++;
        if (slice_ticks < TICKS_PER_SLICE)
            return 0;
    }

    slice_ticks = 0;
    return context_switch_from(current_rsp);
}

uint64_t task_schedule_from_interrupt(InterruptFrame* frame)
{
    kassert(frame != 0, "task_schedule_from_interrupt: null frame");
    return context_switch_from((uint64_t)frame);
}

void schedule(void)
{
    uint64_t next_rsp = context_switch_from(0);
    if (!next_rsp) return;

    __asm__ volatile (
        "movq %0, %%rsp\n"
        "popq %%r15\n" "popq %%r14\n" "popq %%r13\n" "popq %%r12\n"
        "popq %%r11\n" "popq %%r10\n" "popq %%r9\n"  "popq %%r8\n"
        "popq %%rbp\n" "popq %%rdi\n" "popq %%rsi\n" "popq %%rdx\n"
        "popq %%rcx\n" "popq %%rbx\n" "popq %%rax\n"
        "addq $16, %%rsp\n"
        "iretq\n"
        :
        : "r"(next_rsp)
        : "memory"
    );
}

void idle_task(void)
{
    while (1)
        __asm__ volatile ("hlt");
}

void task_print_stats(void)
{
    uint64_t state_new = 0;
    uint64_t state_runnable = 0;
    uint64_t state_running = 0;
    uint64_t state_waiting = 0;
    uint64_t state_zombie = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        switch (tasks[i].state) {
            case TASK_NEW:      state_new++; break;
            case TASK_RUNNABLE: state_runnable++; break;
            case TASK_RUNNING:  state_running++; break;
            case TASK_WAITING:  state_waiting++; break;
            case TASK_ZOMBIE:   state_zombie++; break;
            default: break;
        }
    }

    print("sched: current_tid=");
    if (current_tid < 0) print("none");
    else print_dec((uint64_t)current_tid);
    print(" switches=");
    print_dec(switch_count);
    print("\n");

    print("sched: NEW=");
    print_dec(state_new);
    print(" RUNNABLE=");
    print_dec(state_runnable);
    print(" RUNNING=");
    print_dec(state_running);
    print(" WAITING=");
    print_dec(state_waiting);
    print(" ZOMBIE=");
    print_dec(state_zombie);
    print("\n");

    for (int i = 0; i < MAX_TASKS; i++) {
        Task* t = &tasks[i];
        if (t->state == TASK_NEW) continue;

        print("sched: tid=");
        print_dec((uint64_t)t->tid);
        print(" pid=");
        print_dec((uint64_t)t->pid);
        print(" state=");
        print(state_name(t->state));
        print(" user=");
        print_dec((uint64_t)t->is_user);
        print(" slices=");
        print_dec((uint64_t)t->timeslice);
        print("\n");
    }
}

int task_kill_pid(int pid, int code)
{
    if (pid <= 0) return -1;

    for (int i = 0; i < MAX_TASKS; i++) {
        Task* t = &tasks[i];
        if (t->pid != pid) continue;
        if (t->state == TASK_NEW || t->state == TASK_ZOMBIE) continue;

        if (i == current_tid) {
            task_exit(code);
            return 0;
        }

        t->exit_code = code;
        TaskState from = t->state;
        t->state = TASK_ZOMBIE;
        log_transition("kill", t, from, t->state);
        return 0;
    }

    return -1;
}

int task_self_check(void)
{
    int running_count = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        Task* t = &tasks[i];
        if (t->state == TASK_RUNNING)
            running_count++;

        if (t->is_user && t->state != TASK_NEW) {
            if (t->address_space == 0 || t->cr3 == 0)
                return 0;
        }
    }

    if (running_count > 1)
        return 0;

    if (current_tid >= 0) {
        if (current_tid >= MAX_TASKS)
            return 0;
        if (tasks[current_tid].state != TASK_RUNNING)
            return 0;
    }

    return 1;
}

uint64_t task_get_switch_count(void)
{
    return switch_count;
}
