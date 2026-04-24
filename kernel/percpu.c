#include "percpu.h"
#include "display.h"

#define MSR_KERNEL_GS_BASE 0xC0000102

// 8KB kernel stack for syscall handling
static uint8_t syscall_stack[8192] __attribute__((aligned(16)));
PerCPU percpu_data;

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

extern void tss_set_rsp0(uint64_t rsp);

// void percpu_init(void)
// {
//     percpu_data.kernel_rsp = (uint64_t)syscall_stack + sizeof(syscall_stack);
//     percpu_data.user_rsp   = 0;

//     // set TSS RSP0 to kernel stack so interrupts from ring 3 work
//     tss_set_rsp0((uint64_t)syscall_stack + sizeof(syscall_stack));

//     // KERNEL_GS_BASE points to our PerCPU struct
//     // after swapgs, GS base = &percpu_data
//     wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&percpu_data);

//     print("PerCPU initialized\n");
// }
void percpu_init(void)
{
    percpu_data.kernel_rsp = (uint64_t)syscall_stack + sizeof(syscall_stack);
    percpu_data.user_rsp   = 0;
    percpu_data.kernel_cr3 = read_cr3();  // Save current (kernel) CR3
    percpu_data.user_cr3   = 0;  // Will be set by process_run before iretq

    tss_set_rsp0((uint64_t)syscall_stack + sizeof(syscall_stack));
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&percpu_data);

    print("PerCPU initialized\n");
}