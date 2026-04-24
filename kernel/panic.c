#include "panic.h"
#include "display.h"
#include "serial.h"

void panic(const char* message)
{
    __asm__ volatile ("cli");

    serial_write("\n[PANIC] ");
    serial_write(message);
    serial_write("\n");

    print("\n*** KERNEL PANIC ***\n");
    print(message);
    print("\n");
    display_flush();

    while (1)
        __asm__ volatile ("hlt");
}

void kassert(int condition, const char* message)
{
    if (!condition)
        panic(message);
}
