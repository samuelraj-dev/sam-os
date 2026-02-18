#include "tss.h"
#include "gdt.h"
#include "display.h"

static TSS tss;

// dedicated stack for double fault handler
// so even a stack overflow gets caught
static uint8_t df_stack[4096] __attribute__((aligned(16)));

void tss_init(void)
{
    // zero the whole TSS
    uint8_t* p = (uint8_t*)&tss;
    for (uint64_t i = 0; i < sizeof(TSS); i++) p[i] = 0;

    // RSP0: kernel stack pointer for ring 0
    // when an interrupt fires in user mode, CPU switches to this stack
    // for now point it at our df_stack top (will be updated per-thread later)
    tss.rsp0 = (uint64_t)df_stack + sizeof(df_stack);

    // IST1: dedicated stack for double fault
    tss.ist1 = (uint64_t)df_stack + sizeof(df_stack);

    // IOPB offset beyond TSS size = no I/O port permissions for user mode
    tss.iopb_offset = sizeof(TSS);

    // install TSS descriptor into GDT slots 5+6
    gdt_set_tss((uint64_t)&tss, sizeof(TSS) - 1);

    // load task register — tells CPU where the TSS is
    // 0x28 = GDT_TSS selector, | 0 = RPL 0
    __asm__ volatile ("ltr %0" :: "r"((uint16_t)0x28));

    print("TSS loaded\n");
}