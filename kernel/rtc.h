#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RtcDateTime;

void rtc_init(void);
int  rtc_read_datetime(RtcDateTime* out);
void rtc_format(const RtcDateTime* dt, char* out, uint32_t out_len);

#endif
