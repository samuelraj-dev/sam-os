#include "input.h"
#include "display.h"
#include "shell.h"

#define INPUT_MAX 128

static char input_buffer[INPUT_MAX];
static int input_len = 0;

void input_putc(char c)
{
    if (input_len >= INPUT_MAX - 1)
        return;

    input_buffer[input_len++] = c;
    input_buffer[input_len] = '\0';

    print_char(c); // echo to screen
}

void input_backspace(void)
{
    if (input_len <= 0)
        return;

    input_len--;
    input_buffer[input_len] = '\0';

    // visually erase
    print("\b \b");
}

void input_submit(void)
{
    print("\n");

    shell_execute(input_buffer);

    input_len = 0;
    input_buffer[0] = '\0';

    print("sam> ");
}