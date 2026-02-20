#include "percpu.h"
#include "display.h"

#define MSR_KERNEL_GS_BASE 0xC0000102

// 8KB kernel stack for syscall handling
static uint8_t syscall_stack[8192] __attribute__((aligned(16)));
static PerCPU percpu_data;

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

void percpu_init(void)
{
    percpu_data.kernel_rsp = (uint64_t)syscall_stack + sizeof(syscall_stack);
    percpu_data.user_rsp   = 0;

    // KERNEL_GS_BASE points to our PerCPU struct
    // after swapgs, GS base = &percpu_data
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&percpu_data);

    print("PerCPU initialized\n");
}