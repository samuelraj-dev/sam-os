#include "syscall.h"
#include "syscall_abi.h"
#include "scheduler/task.h"
#include "process.h"
#include "display.h"
#include "vmm.h"

#define USER_TOP 0x0000800000000000ULL

static uint64_t ret_err(uint64_t errno_val)
{
    return 0ULL - errno_val;
}

static int user_range_valid(AddressSpace* as, uint64_t start, uint64_t len)
{
    if (!as) return 0;
    if (len == 0) return 1;
    if (start >= USER_TOP) return 0;
    if (start + len < start) return 0;
    if (start + len > USER_TOP) return 0;

    uint64_t from = start & ~0xFFFULL;
    uint64_t to = (start + len - 1) & ~0xFFFULL;

    for (uint64_t va = from; va <= to; va += 0x1000) {
        if (!vmm_virt_to_phys(as, va))
            return 0;
    }

    return 1;
}

static uint64_t sys_write(InterruptFrame* frame)
{
    Task* t = task_current();
    if (!t || !t->is_user) return ret_err(1);

    uint64_t user_ptr = frame->rbx;
    uint64_t len = frame->rcx;

    if (len > 4096) return ret_err(22);
    if (!user_range_valid(t->address_space, user_ptr, len))
        return ret_err(14);

    char* s = (char*)user_ptr;
    for (uint64_t i = 0; i < len; i++) {
        char out[2] = { s[i], 0 };
        print(out);
    }
    display_flush();

    return len;
}

static uint64_t sys_exit(InterruptFrame* frame)
{
    int code = (int)(frame->rbx & 0xFFFFFFFFULL);
    int pid = task_current_pid();
    process_task_exited(pid, code);

    Task* t = task_current();
    if (t) {
        t->exit_code = code;
        t->state = TASK_ZOMBIE;
    }

    return ret_err(11);
}

static uint64_t sys_yield(void)
{
    return 0;
}

static uint64_t sys_spawn(InterruptFrame* frame)
{
    int image_id = (int)(frame->rbx & 0xFFFFFFFFULL);
    int pid = process_spawn_builtin(image_id);
    if (pid < 0)
        return ret_err(12);
    return (uint64_t)pid;
}

static uint64_t sys_getpid(void)
{
    int pid = task_current_pid();
    if (pid < 0) return ret_err(3);
    return (uint64_t)pid;
}

uint64_t syscall_dispatch(InterruptFrame* frame)
{
    if (!frame) return 0;

    uint64_t ret = ret_err(38);

    switch (frame->rax) {
        case SYS_WRITE:  ret = sys_write(frame); break;
        case SYS_EXIT:   ret = sys_exit(frame); break;
        case SYS_YIELD:  ret = sys_yield(); break;
        case SYS_SPAWN:  ret = sys_spawn(frame); break;
        case SYS_GETPID: ret = sys_getpid(); break;
        default:         ret = ret_err(38); break;
    }

    frame->rax = ret;

    if (frame->rax == ret_err(11) || frame->rax == 0) {
        Task* t = task_current();
        if (t && t->state != TASK_WAITING && t->state != TASK_ZOMBIE)
            t->state = TASK_RUNNABLE;
        return task_schedule_from_interrupt(frame);
    }

    return 0;
}
