#include "serial.h"

#define COM1 0x3F8

static int serial_ready = 0;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void serial_init(void)
{
    outb(COM1 + 1, 0x00);    // disable interrupts
    outb(COM1 + 3, 0x80);    // enable divisor latch
    outb(COM1 + 0, 0x03);    // 38400 baud divisor low
    outb(COM1 + 1, 0x00);    // divisor high
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);    // enable FIFO, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    serial_ready = 1;
}

void serial_write_char(char c)
{
    if (!serial_ready)
        return;

    if (c == '\n')
        serial_write_char('\r');

    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (uint8_t)c);
}

void serial_write(const char* str)
{
    if (!str)
        return;

    while (*str)
        serial_write_char(*str++);
}

void serial_write_hex(uint64_t value)
{
    const char* hex = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_write_char(hex[(value >> shift) & 0xF]);
}

void serial_write_dec(uint64_t value)
{
    char buf[32];
    int i = 0;

    if (value == 0) {
        serial_write_char('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
        serial_write_char(buf[--i]);
}
