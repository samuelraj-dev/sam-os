#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "types.h"

typedef struct {
    void* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    void* memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_descriptor_size;
} BootInfo;

#endif