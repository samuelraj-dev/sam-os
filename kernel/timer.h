#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void timer_init(uint32_t frequency);
int timer_flush_needed(void);
uint64_t timer_get_ticks(void);
void timer_handler(void);

#endif