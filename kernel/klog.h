#ifndef KLOG_H
#define KLOG_H

#include "types.h"

void klog_init(void);
void klog_info(const char* message);
void klog_warn(const char* message);
void klog_error(const char* message);
void klog_panic(const char* message);
void klog_hex(uint64_t value);
void klog_dec(uint64_t value);
void klog_newline(void);

#endif
