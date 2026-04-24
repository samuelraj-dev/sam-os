#ifndef KMALLOC_H
#define KMALLOC_H

#include "types.h"

void kmalloc_init(void);
void* kmalloc(uint64_t size);
void* kzalloc(uint64_t size);
void kfree(void* ptr);

#endif
