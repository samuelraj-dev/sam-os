#include "syscall.h"
#include "display.h"

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

// called from syscall_entry.S
// rdi=num, rsi=arg0, rdx=arg1, rcx=arg2
void syscall_handler(uint64_t num, uint64_t arg0,
                     uint64_t arg1, uint64_t arg2)
{
    (void)arg2;
    switch (num) {
        case SYS_WRITE: {
            // arg0 = string pointer (user virtual address)
            // arg1 = length
            char* str = (char*)arg0;
            for (uint64_t i = 0; i < arg1; i++) {
                char c[2] = { str[i], 0 };
                print(c);
            }
            break;
        }
        case SYS_EXIT:
            print("[process exited]\n");
            while(1) __asm__ volatile ("hlt");
            break;
        default:
            print("unknown syscall: ");
            print_dec(num);
            print("\n");
            break;
    }
}

void syscall_init(void)
{
    // enable SCE in EFER
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1;
    wrmsr(MSR_EFER, efer);

    // STAR:
    // bits 47:32 = kernel CS = 0x08
    // bits 63:48 = user CS base = 0x18
    // sysret does: CS = 0x18|3 = 0x1B, SS = 0x20|3 = 0x23
    uint64_t star = ((uint64_t)0x0008 << 32) |
                    ((uint64_t)0x0018 << 48);
    wrmsr(MSR_STAR, star);

    // LSTAR = syscall entry point
    extern void syscall_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    // SFMASK = clear IF on syscall entry (disable interrupts)
    wrmsr(MSR_SFMASK, (1 << 9));

    print("Syscall initialized\n");
}