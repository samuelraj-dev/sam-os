// no libc — everything manual

static void syscall_write(const char* str, unsigned long len) {
    __asm__ volatile (
        "syscall"
        :
        : "D"(0UL),   // rdi = SYS_WRITE
          "S"(str),   // rsi = arg0 = string pointer
          "d"(len)    // rdx = arg1 = length
        : "rcx", "r11", "memory"
    );
}

static void syscall_exit(void) {
    __asm__ volatile (
        "syscall"
        :
        : "D"(1UL)    // rdi = SYS_EXIT
        : "rcx", "r11"
    );
}

void _start(void) {
    syscall_write("Hello from userspace!\n", 22);
    syscall_exit();
    while(1);  // never reached
}