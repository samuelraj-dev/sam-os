#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "idt.h"

uint64_t syscall_dispatch(InterruptFrame* frame);

#endif
