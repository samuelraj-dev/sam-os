#include "rtc.h"
#include "io.h"
#include "klog.h"

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, (uint8_t)(reg | 0x80));
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

static uint8_t decode_hour(uint8_t raw, int is_bcd, int is_24h)
{
    uint8_t hour = raw;
    int pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (is_bcd)
        hour = bcd_to_bin(hour);

    if (!is_24h) {
        if (pm && hour < 12) hour = (uint8_t)(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }

    return hour;
}

static int rtc_is_updating(void)
{
    return (cmos_read(0x0A) & 0x80) != 0;
}

static void copy_dt(RtcDateTime* dst, const RtcDateTime* src)
{
    dst->year = src->year;
    dst->month = src->month;
    dst->day = src->day;
    dst->hour = src->hour;
    dst->minute = src->minute;
    dst->second = src->second;
}

static int dt_equal(const RtcDateTime* a, const RtcDateTime* b)
{
    return a->year == b->year &&
           a->month == b->month &&
           a->day == b->day &&
           a->hour == b->hour &&
           a->minute == b->minute &&
           a->second == b->second;
}

static void rtc_read_raw(RtcDateTime* out, uint8_t* reg_b)
{
    out->second = cmos_read(0x00);
    out->minute = cmos_read(0x02);
    out->hour = cmos_read(0x04);
    out->day = cmos_read(0x07);
    out->month = cmos_read(0x08);
    out->year = cmos_read(0x09);
    *reg_b = cmos_read(0x0B);
}

void rtc_init(void)
{
    klog_info("RTC: CMOS wall clock ready");
}

int rtc_read_datetime(RtcDateTime* out)
{
    if (!out) return 0;

    RtcDateTime prev;
    RtcDateTime cur;
    uint8_t reg_b_prev = 0;
    uint8_t reg_b_cur = 0;

    while (rtc_is_updating()) {}
    rtc_read_raw(&prev, &reg_b_prev);

    while (1) {
        while (rtc_is_updating()) {}
        rtc_read_raw(&cur, &reg_b_cur);
        if (dt_equal(&prev, &cur))
            break;
        copy_dt(&prev, &cur);
        reg_b_prev = reg_b_cur;
    }

    uint8_t reg_b = reg_b_cur;

    int is_bcd = ((reg_b & 0x04) == 0);
    int is_24h = ((reg_b & 0x02) != 0);
    if (is_bcd) {
        cur.second = bcd_to_bin(cur.second);
        cur.minute = bcd_to_bin(cur.minute);
        cur.day = bcd_to_bin(cur.day);
        cur.month = bcd_to_bin(cur.month);
        cur.year = bcd_to_bin((uint8_t)cur.year);
    }
    cur.hour = decode_hour(cur.hour, is_bcd, is_24h);

    cur.year = (uint16_t)(2000 + (cur.year % 100));
    copy_dt(out, &cur);
    return 1;
}

static void append_2(char* out, uint32_t* pos, uint32_t max, uint32_t value)
{
    if (*pos + 2 >= max) return;
    out[(*pos)++] = (char)('0' + ((value / 10) % 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void append_4(char* out, uint32_t* pos, uint32_t max, uint32_t value)
{
    if (*pos + 4 >= max) return;
    out[(*pos)++] = (char)('0' + ((value / 1000) % 10));
    out[(*pos)++] = (char)('0' + ((value / 100) % 10));
    out[(*pos)++] = (char)('0' + ((value / 10) % 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

void rtc_format(const RtcDateTime* dt, char* out, uint32_t out_len)
{
    if (!out || out_len == 0) return;

    if (!dt) {
        out[0] = '\0';
        return;
    }

    uint32_t p = 0;
    append_4(out, &p, out_len, dt->year);
    if (p + 1 < out_len) out[p++] = '-';
    append_2(out, &p, out_len, dt->month);
    if (p + 1 < out_len) out[p++] = '-';
    append_2(out, &p, out_len, dt->day);
    if (p + 1 < out_len) out[p++] = ' ';
    append_2(out, &p, out_len, dt->hour);
    if (p + 1 < out_len) out[p++] = ':';
    append_2(out, &p, out_len, dt->minute);
    if (p + 1 < out_len) out[p++] = ':';
    append_2(out, &p, out_len, dt->second);

    if (p >= out_len)
        p = out_len - 1;
    out[p] = '\0';
}
