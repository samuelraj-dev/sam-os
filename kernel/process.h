#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "vmm.h"

#define PROCESS_STACK_VIRT  0x00007FFFFFFFE000ULL
#define PROCESS_STACK_PAGES 4
#define MAX_PROCESSES       16

typedef struct {
    int           used;
    int           pid;
    AddressSpace* address_space;
    int           main_tid;
    uint64_t      entry;
    uint64_t      user_rsp;
    int           exited;
    int           exit_code;
    int           reap_pending;
} Process;

void process_init(void);
int  process_spawn_from_elf(const void* elf_data, uint64_t size);
int  process_spawn_builtin(int image_id);
void process_task_exited(int pid, int code);
void process_reap_deferred(void);
void process_dump_table(void);
int  process_kill(int pid, int code);
int  process_wait_poll(int pid, int* out_code);
int  process_is_active(int pid);
uint64_t process_count_active(void);

#endif
