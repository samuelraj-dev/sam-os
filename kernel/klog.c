#include "klog.h"
#include "display.h"
#include "serial.h"

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
