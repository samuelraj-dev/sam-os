#include "shell.h"
#include "display.h"
#include "timer.h"
#include "string.h"

void shell_execute(const char* cmd)
{
    if (kstrcmp(cmd, "help") == 0) {
        print("Commands: help, ticks, clear\n");
    }
    else if (kstrcmp(cmd, "ticks") == 0) {
        uint64_t t = timer_get_ticks();
        print("Ticks: ");
        print_dec(t);
        print("\n");
    }
    else if (kstrcmp(cmd, "clear") == 0) {
        display_clear();
    }
    else if (cmd[0] != '\0') {
        print("Unknown command\n");
    }
}