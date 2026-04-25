#include "klog.h"
#include "display.h"
#include "serial.h"

#define KLOG_RING_LINES 64
#define KLOG_LINE_MAX   96

static char g_ring[KLOG_RING_LINES][KLOG_LINE_MAX];
static uint32_t g_ring_head = 0;
static uint32_t g_ring_count = 0;

static void ring_write_line(const char* level, const char* message)
{
    char* out = g_ring[g_ring_head];
    uint32_t p = 0;

    out[p++] = '[';
    for (uint32_t i = 0; level[i] && p < KLOG_LINE_MAX - 2; i++)
        out[p++] = level[i];
    out[p++] = ']';
    if (p < KLOG_LINE_MAX - 1) out[p++] = ' ';

    if (!message) message = "(null)";
    for (uint32_t i = 0; message[i] && p < KLOG_LINE_MAX - 1; i++)
        out[p++] = message[i];

    out[p] = '\0';

    g_ring_head = (g_ring_head + 1) % KLOG_RING_LINES;
    if (g_ring_count < KLOG_RING_LINES)
        g_ring_count++;
}

static void klog_prefix(const char* level)
{
    print("[");
    print(level);
    print("] ");
    serial_write("[");
    serial_write(level);
    serial_write("] ");
}

static void klog_line(const char* level, const char* message)
{
    ring_write_line(level, message);
    klog_prefix(level);
    print(message);
    print("\n");
    serial_write(message);
    serial_write("\n");
}

void klog_init(void)
{
    serial_init();
    serial_write("\n[INFO] serial online\n");
}

void klog_info(const char* message)  { klog_line("INFO", message); }
void klog_warn(const char* message)  { klog_line("WARN", message); }
void klog_error(const char* message) { klog_line("ERROR", message); }
void klog_panic(const char* message) { klog_line("PANIC", message); }

void klog_hex(uint64_t value)
{
    print_hex(value);
    serial_write_hex(value);
}

void klog_dec(uint64_t value)
{
    print_dec(value);
    serial_write_dec(value);
}

void klog_newline(void)
{
    print("\n");
    serial_write("\n");
}

void klog_dump_recent(uint32_t limit)
{
    if (g_ring_count == 0) {
        print("dmesg: empty\n");
        return;
    }

    if (limit == 0 || limit > g_ring_count)
        limit = g_ring_count;

    uint32_t start = (g_ring_head + KLOG_RING_LINES - limit) % KLOG_RING_LINES;
    for (uint32_t i = 0; i < limit; i++) {
        uint32_t idx = (start + i) % KLOG_RING_LINES;
        print(g_ring[idx]);
        print("\n");
    }
}
