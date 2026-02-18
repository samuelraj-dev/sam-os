#ifndef IDT_H
#define IDT_H

#include "types.h"

typedef struct {
    uint16_t offset_low;   // handler address bits 0-15
    uint16_t selector;     // GDT code segment selector
    uint8_t  ist;          // IST index (0 = none, 1 = use IST1)
    uint8_t  type_attr;    // type and privilege flags
    uint16_t offset_mid;   // handler address bits 16-31
    uint32_t offset_high;  // handler address bits 32-63
    uint32_t reserved;
} __attribute__((packed)) IDTEntry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) IDTDescriptor;

// CPU pushes this on the stack before calling your handler
typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9,  r8;
    uint64_t rbp, rdi, rsi, rdx;
    uint64_t rcx, rbx, rax;
    uint64_t vector;       // which exception
    uint64_t error_code;   // pushed by CPU (or 0 if none)
    uint64_t rip;          // where the fault happened
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} InterruptFrame;

void idt_init(void);

#endif