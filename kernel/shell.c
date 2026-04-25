#include "shell.h"
#include "display.h"
#include "timer.h"
#include "string.h"
#include "keyboard.h"
#include "scheduler/task.h"
#include "acpi.h"
#include "process.h"
#include "syscall_abi.h"
#include "rtc.h"
#include "irq.h"
#include "pmm.h"
#include "klog.h"
#include "panic.h"

#define INPUT_MAX 256
#define HISTORY_MAX 16

static char input_buf[INPUT_MAX];
static int  input_len = 0;
static int  input_overflow = 0;

static char history[HISTORY_MAX][INPUT_MAX];
static int history_count = 0;
static int history_cursor = -1;

static int watch_irq = 0;
static int watch_sched = 0;

static int str_prefix(const char* s, const char* prefix)
{
    while (*prefix) {
        if (*s != *prefix)
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static const char* skip_spaces(const char* s)
{
    while (*s == ' ') s++;
    return s;
}

static int parse_u64(const char* s, uint64_t* out)
{
    s = skip_spaces(s);
    if (!*s) return 0;

    uint64_t v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        v = v * 10 + (uint64_t)(*s - '0');
        s++;
    }

    if (!any) return 0;
    *out = v;
    return 1;
}

static void print_prompt(void)
{
    RtcDateTime dt;
    char ts[24];
    if (rtc_read_datetime(&dt)) {
        rtc_format(&dt, ts, sizeof(ts));
        print("[");
        print(ts);
        print("] ");
    }
    print("sam> ");
}

static void clear_typed_line(void)
{
    while (input_len > 0) {
        print("\b \b");
        input_len--;
    }
    input_buf[0] = '\0';
}

static void history_push(const char* cmd)
{
    if (!cmd || !cmd[0]) return;

    if (history_count > 0) {
        int last = (history_count - 1) % HISTORY_MAX;
        if (kstrcmp(history[last], cmd) == 0)
            return;
    }

    int idx = history_count % HISTORY_MAX;
    int i = 0;
    for (; cmd[i] && i < INPUT_MAX - 1; i++)
        history[idx][i] = cmd[i];
    history[idx][i] = '\0';
    history_count++;
}

static void history_apply(int absolute_index)
{
    int oldest = (history_count > HISTORY_MAX) ? (history_count - HISTORY_MAX) : 0;
    if (absolute_index < oldest || absolute_index >= history_count)
        return;

    int idx = absolute_index % HISTORY_MAX;
    clear_typed_line();

    const char* src = history[idx];
    while (*src && input_len < INPUT_MAX - 1) {
        input_buf[input_len++] = *src;
        char s[2] = {*src, 0};
        print(s);
        src++;
    }
    input_buf[input_len] = '\0';
    display_flush();
}

static void handle_history_up(void)
{
    if (history_count <= 0) return;

    if (history_cursor < 0)
        history_cursor = history_count - 1;
    else if (history_cursor > 0)
        history_cursor--;

    history_apply(history_cursor);
}

static void handle_history_down(void)
{
    if (history_count <= 0 || history_cursor < 0)
        return;

    history_cursor++;
    if (history_cursor >= history_count) {
        history_cursor = -1;
        clear_typed_line();
        display_flush();
        return;
    }

    history_apply(history_cursor);
}

static void print_irq_stats(void)
{
    IRQStats st;
    irq_get_stats(&st);

    print("irqstat: backend=");
    print(irq_controller_name());
    print(" timer=");
    print_dec(st.timer_irq_count);
    print(" keyboard=");
    print_dec(st.keyboard_irq_count);
    print(" spurious=");
    print_dec(st.spurious_irq_count);
    print(" eoi=");
    print_dec(st.eoi_count);
    print(" route_fail=");
    print_dec(st.apic_route_failures);
    print(" health_fault=");
    print_dec(st.health_faults);
    print("\n");
}

static void print_status(void)
{
    uint64_t ticks = timer_get_ticks();
    uint32_t hz = timer_get_frequency();
    uint64_t up_s = (hz ? (ticks / hz) : 0);

    print("status: irq=");
    print(irq_controller_name());
    print(" ticks=");
    print_dec(ticks);
    print(" up_s=");
    print_dec(up_s);
    print(" free_pages=");
    print_dec(pmm_get_free_pages());
    print(" procs=");
    print_dec(process_count_active());
    print(" switches=");
    print_dec(task_get_switch_count());
    print("\n");
}

static void run_watch_if_enabled(void)
{
    if (watch_irq)
        print_irq_stats();
    if (watch_sched)
        task_print_stats();
}

static void process_command(void)
{
    input_buf[input_len] = '\0';

    if (input_len > 0)
        history_push(input_buf);
    history_cursor = -1;

    if (kstrcmp(input_buf, "help") == 0) {
        print("commands: help clear ticks sched schedcheck about shutdown spawnh spawnb time irq irqstat clocksrc hw ps kill wait memstat uptime status dmesg-lite watch\n");
    } else if (kstrcmp(input_buf, "clear") == 0) {
        display_clear();
    } else if (kstrcmp(input_buf, "ticks") == 0) {
        uint64_t t = timer_get_ticks();
        print("ticks: ");
        print_dec(t);
        print("\n");
    } else if (kstrcmp(input_buf, "about") == 0) {
        print("SamOS built from scratch\n");
    } else if (kstrcmp(input_buf, "sched") == 0) {
        task_print_stats();
    } else if (kstrcmp(input_buf, "schedcheck") == 0) {
        int ok = task_self_check();
        print("schedcheck: ");
        print(ok ? "ok" : "fail");
        print("\n");
    } else if (kstrcmp(input_buf, "spawnh") == 0) {
        int pid = process_spawn_builtin(SPAWN_IMAGE_HELLO);
        print("spawn hello pid=");
        if (pid < 0) print("err\n"); else { print_dec((uint64_t)pid); print("\n"); }
    } else if (kstrcmp(input_buf, "spawnb") == 0) {
        int pid = process_spawn_builtin(SPAWN_IMAGE_BURN);
        print("spawn burn pid=");
        if (pid < 0) print("err\n"); else { print_dec((uint64_t)pid); print("\n"); }
    } else if (kstrcmp(input_buf, "ps") == 0) {
        process_dump_table();
    } else if (str_prefix(input_buf, "kill")) {
        uint64_t pid = 0;
        uint64_t code = 137;
        const char* rest = skip_spaces(input_buf + 4);
        if (!parse_u64(rest, &pid)) {
            print("kill: usage kill <pid> [code]\n");
        } else {
            while (*rest && *rest != ' ') rest++;
            if (*rest == ' ') {
                uint64_t parsed = 0;
                if (parse_u64(rest, &parsed)) code = parsed;
            }
            int rc = process_kill((int)pid, (int)code);
            if (rc < 0) print("kill: no such pid\n");
            else if (rc > 0) print("kill: already exited\n");
            else {
                print("kill: sent to pid ");
                print_dec(pid);
                print("\n");
            }
        }
    } else if (str_prefix(input_buf, "wait")) {
        uint64_t pid = 0;
        const char* rest = skip_spaces(input_buf + 4);
        if (!parse_u64(rest, &pid)) {
            print("wait: usage wait <pid>\n");
        } else {
            int code = 0;
            while (1) {
                process_reap_deferred();
                int rc = process_wait_poll((int)pid, &code);
                if (rc < 0) {
                    print("wait: no such pid\n");
                    break;
                }
                if (rc > 0) {
                    print("wait: pid ");
                    print_dec(pid);
                    print(" exit=");
                    print_dec((uint64_t)code);
                    print("\n");
                    break;
                }
                task_yield();
            }
        }
    } else if (kstrcmp(input_buf, "time") == 0) {
        RtcDateTime dt;
        char ts[24];
        if (rtc_read_datetime(&dt)) {
            rtc_format(&dt, ts, sizeof(ts));
            print("time: ");
            print(ts);
            print("\n");
        } else {
            print("time: unavailable\n");
        }
    } else if (kstrcmp(input_buf, "irq") == 0) {
        irq_dump_routes();
    } else if (kstrcmp(input_buf, "irqstat") == 0) {
        print_irq_stats();
    } else if (kstrcmp(input_buf, "clocksrc") == 0) {
        print("clock source: PIT(");
        print_dec((uint64_t)timer_get_frequency());
        print("Hz) + CMOS RTC\n");
    } else if (kstrcmp(input_buf, "memstat") == 0) {
        print("mem: free_pages=");
        print_dec(pmm_get_free_pages());
        print(" total_pages=");
        print_dec(pmm_get_total_pages());
        print("\n");
    } else if (kstrcmp(input_buf, "uptime") == 0) {
        uint64_t ticks = timer_get_ticks();
        uint32_t hz = timer_get_frequency();
        print("uptime: ");
        print_dec(hz ? (ticks / hz) : 0);
        print(" s\n");
    } else if (kstrcmp(input_buf, "status") == 0) {
        print_status();
    } else if (str_prefix(input_buf, "dmesg-lite")) {
        uint64_t n = 32;
        const char* rest = skip_spaces(input_buf + 10);
        uint64_t parsed = 0;
        if (parse_u64(rest, &parsed)) n = parsed;
        klog_dump_recent((uint32_t)n);
    } else if (str_prefix(input_buf, "watch")) {
        const char* rest = skip_spaces(input_buf + 5);
        if (kstrcmp(rest, "irq") == 0) {
            watch_irq = !watch_irq;
            print("watch irq: ");
            print(watch_irq ? "on\n" : "off\n");
        } else if (kstrcmp(rest, "sched") == 0) {
            watch_sched = !watch_sched;
            print("watch sched: ");
            print(watch_sched ? "on\n" : "off\n");
        } else {
            print("watch: usage watch irq|sched\n");
        }
    } else if (kstrcmp(input_buf, "hw") == 0) {
        const AcpiMadtInfo* madt = acpi_get_madt_info();
        if (!madt || !madt->present) {
            print("hw: ACPI MADT unavailable\n");
        } else {
            print("hw: LAPIC=");
            print_hex(madt->lapic_addr);
            print(" IOAPIC=");
            print_hex(madt->ioapic_addr);
            print(" GSI_BASE=");
            print_dec(madt->ioapic_gsi_base);
            print(" CPUs=");
            print_dec(madt->cpu_lapic_count);
            print("\n");
        }
    } else if (kstrcmp(input_buf, "shutdown") == 0) {
        print("SamOS shutting down\n");
        shutdown();
    } else if (kstrcmp(input_buf, "paniclast") == 0) {
        print("paniclast: ");
        print(panic_get_last());
        print("\n");
    } else if (input_len > 0) {
        print("unknown: ");
        print(input_buf);
        print("\n");
    }

    process_reap_deferred();
    run_watch_if_enabled();

    input_len = 0;
    input_overflow = 0;
    print_prompt();
    display_flush();
}

void shell_task(void)
{
    print_prompt();
    display_flush();

    while (1) {
        task_sleep();

        char c;
        while ((c = kb_getchar()) != 0) {
            if (c == '\n' || c == '\r') {
                print("\n");
                process_command();
            } else if (c == '\b') {
                if (input_len > 0) {
                    input_len--;
                    input_buf[input_len] = '\0';
                    print("\b \b");
                    display_flush();
                }
            } else if (c == KB_EVENT_UP) {
                handle_history_up();
            } else if (c == KB_EVENT_DOWN) {
                handle_history_down();
            } else if (c >= 32 && c <= 126) {
                if (input_len < INPUT_MAX - 1) {
                    input_buf[input_len++] = c;
                    input_buf[input_len] = '\0';
                    char s[2] = {c, 0};
                    print(s);
                    display_flush();
                } else if (!input_overflow) {
                    input_overflow = 1;
                    print("\n[input buffer full]\n");
                    print_prompt();
                    for (int i = 0; i < input_len; i++) {
                        char s[2] = {input_buf[i], 0};
                        print(s);
                    }
                    display_flush();
                }
            }
        }
    }
}
