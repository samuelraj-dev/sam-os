#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void     timer_init(uint32_t frequency);
uint64_t timer_tick(uint64_t current_rsp);
int      timer_flush_needed(void);
uint64_t timer_get_ticks(void);

#endif