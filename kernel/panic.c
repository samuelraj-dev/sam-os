#include "panic.h"
#include "display.h"
#include "serial.h"
#include "scheduler/task.h"

static char g_last_panic[128];

static void panic_store_last(const char* message)
{
    uint32_t i = 0;
    if (!message) message = "(null)";
    for (; message[i] && i < sizeof(g_last_panic) - 1; i++)
        g_last_panic[i] = message[i];
    g_last_panic[i] = '\0';
}

void panic(const char* message)
{
    __asm__ volatile ("cli");

    panic_store_last(message);

    serial_write("\n[PANIC] ");
    serial_write(message);
    serial_write("\n");

    print("\n*** KERNEL PANIC ***\n");
    print(message);
    print("\n");

    Task* t = task_current();
    if (t) {
        print("tid=");
        print_dec((uint64_t)t->tid);
        print(" pid=");
        print_dec((uint64_t)t->pid);
        print(" cr3=");
        print_hex(t->cr3);
        print("\n");

        serial_write("tid=");
        serial_write_dec((uint64_t)t->tid);
        serial_write(" pid=");
        serial_write_dec((uint64_t)t->pid);
        serial_write(" cr3=");
        serial_write_hex(t->cr3);
        serial_write("\n");
    }

    display_flush();

    while (1)
        __asm__ volatile ("hlt");
}

void kassert(int condition, const char* message)
{
    if (!condition)
        panic(message);
}

const char* panic_get_last(void)
{
    return g_last_panic;
}
