#ifndef MMIO_H
#define MMIO_H

#include "types.h"

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t*)addr;
}

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
    *(volatile uint32_t*)addr = value;
}

#endif
