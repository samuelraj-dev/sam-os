// tss.h
#ifndef TSS_H
#define TSS_H

#include "types.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;        // kernel stack for ring 0
    uint64_t rsp1;        // unused for now
    uint64_t rsp2;        // unused for now
    uint64_t reserved1;
    uint64_t ist1;        // interrupt stack 1 — for double fault
    uint64_t ist2;        // unused for now
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset; // I/O permission bitmap offset
} __attribute__((packed)) TSS;

void tss_init(void);

#endif