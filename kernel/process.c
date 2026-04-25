#include "process.h"
#include "scheduler/task.h"
#include "display.h"
#include "klog.h"
#include "pmm.h"
#include "paging.h"
#include "syscall_abi.h"

#define PT_LOAD   1
#define PF_X      1
#define PF_W      2

#define USER_LOWER_LIMIT 0x0000000000400000ULL
#define USER_UPPER_LIMIT 0x0000800000000000ULL

extern uint8_t _binary_user_hello_elf_start;
extern uint8_t _binary_user_hello_elf_end;
extern uint8_t _binary_user_burn_elf_start;
extern uint8_t _binary_user_burn_elf_end;

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

static Process proc_table[MAX_PROCESSES];
static int next_pid = 1;

static int alloc_process_slot(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process* p = &proc_table[i];
        if (!p->used)
            return i;
        if (p->used && p->exited && p->address_space == 0 && p->main_tid < 0)
            return i;
    }
    return -1;
}

static Process* find_process_by_pid(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process* p = &proc_table[i];
        if (!p->used) continue;
        if (p->pid == pid) return p;
    }
    return 0;
}

void process_init(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_table[i].used = 0;
        proc_table[i].pid = 0;
        proc_table[i].address_space = 0;
        proc_table[i].main_tid = -1;
        proc_table[i].entry = 0;
        proc_table[i].user_rsp = 0;
        proc_table[i].exited = 0;
        proc_table[i].exit_code = 0;
        proc_table[i].reap_pending = 0;
    }
    next_pid = 1;
    klog_info("process: table initialized");
}

static int map_user_stack(AddressSpace* as, uint64_t* out_rsp)
{
    if (!as || !out_rsp) return -1;

    uint64_t stack_base = PROCESS_STACK_VIRT - (uint64_t)PROCESS_STACK_PAGES * 0x1000ULL;
    if (stack_base < USER_LOWER_LIMIT || PROCESS_STACK_VIRT >= USER_UPPER_LIMIT)
        return -1;

    for (int i = 0; i < PROCESS_STACK_PAGES; i++) {
        void* page = pmm_alloc_page();
        if (!page) return -1;

        uint8_t* vpage = (uint8_t*)page;
        for (int j = 0; j < 4096; j++) vpage[j] = 0;

        uint64_t va = PROCESS_STACK_VIRT - (uint64_t)(i + 1) * 0x1000;
        vmm_map(as, va, (uint64_t)page,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_NX);
    }

    *out_rsp = PROCESS_STACK_VIRT;
    return 0;
}

static int load_elf_into_as(AddressSpace* as, const void* elf_data, uint64_t size,
                            uint64_t* out_entry)
{
    if (!as || !out_entry || !elf_data || size < sizeof(Elf64_Ehdr)) return -1;

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_data;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F')
        return -1;

    if (ehdr->e_entry < USER_LOWER_LIMIT || ehdr->e_entry >= USER_UPPER_LIMIT)
        return -1;

    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr) > size)
        return -1;

    Elf64_Phdr* phdrs = (Elf64_Phdr*)((uint8_t*)elf_data + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz < ph->p_filesz) return -1;
        if (ph->p_offset + ph->p_filesz > size) return -1;

        uint64_t seg_start = ph->p_vaddr & ~0xFFFULL;
        uint64_t seg_end = (ph->p_vaddr + ph->p_memsz + 0xFFFULL) & ~0xFFFULL;
        if (seg_start < USER_LOWER_LIMIT || seg_end >= USER_UPPER_LIMIT)
            return -1;

        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (ph->p_flags & PF_W) flags |= VMM_FLAG_WRITE;
        if (!(ph->p_flags & PF_X)) flags |= VMM_FLAG_NX;

        for (uint64_t va = seg_start; va < seg_end; va += 0x1000) {
            if (vmm_virt_to_phys(as, va))
                continue;

            void* page = pmm_alloc_page();
            if (!page) return -1;

            uint8_t* vpage = (uint8_t*)page;
            for (int k = 0; k < 4096; k++) vpage[k] = 0;

            vmm_map(as, va, (uint64_t)page, flags);
        }

        uint8_t* src = (uint8_t*)elf_data + ph->p_offset;
        for (uint64_t off = 0; off < ph->p_filesz; off++) {
            uint64_t va = ph->p_vaddr + off;
            uint64_t phys = vmm_virt_to_phys(as, va);
            if (!phys) return -1;
            *((uint8_t*)phys) = src[off];
        }
    }

    *out_entry = ehdr->e_entry;
    return 0;
}

int process_spawn_from_elf(const void* elf_data, uint64_t size)
{
    int slot = alloc_process_slot();
    if (slot < 0) return -1;

    AddressSpace* as = vmm_create_address_space();
    if (!as) return -1;

    uint64_t entry = 0;
    if (load_elf_into_as(as, elf_data, size, &entry) < 0) {
        vmm_destroy_address_space(as);
        return -1;
    }

    uint64_t user_rsp = 0;
    if (map_user_stack(as, &user_rsp) < 0) {
        vmm_destroy_address_space(as);
        return -1;
    }

    int pid = next_pid++;
    int tid = task_create_user(pid, as, entry, user_rsp);
    if (tid < 0) {
        vmm_destroy_address_space(as);
        return -1;
    }

    proc_table[slot].used = 1;
    proc_table[slot].pid = pid;
    proc_table[slot].address_space = as;
    proc_table[slot].main_tid = tid;
    proc_table[slot].entry = entry;
    proc_table[slot].user_rsp = user_rsp;
    proc_table[slot].exited = 0;
    proc_table[slot].exit_code = 0;
    proc_table[slot].reap_pending = 0;

    print("[proc spawn pid=");
    print_dec((uint64_t)pid);
    print(" tid=");
    print_dec((uint64_t)tid);
    print(" entry=");
    print_hex(entry);
    print("]\n");

    return pid;
}

int process_spawn_builtin(int image_id)
{
    if (image_id == SPAWN_IMAGE_HELLO) {
        uint64_t size = (uint64_t)(&_binary_user_hello_elf_end - &_binary_user_hello_elf_start);
        return process_spawn_from_elf(&_binary_user_hello_elf_start, size);
    }

    if (image_id == SPAWN_IMAGE_BURN) {
        uint64_t size = (uint64_t)(&_binary_user_burn_elf_end - &_binary_user_burn_elf_start);
        return process_spawn_from_elf(&_binary_user_burn_elf_start, size);
    }

    return -1;
}

void process_task_exited(int pid, int code)
{
    Process* p = find_process_by_pid(pid);
    if (!p) return;

    print("[proc exit pid=");
    print_dec((uint64_t)pid);
    print(" code=");
    print_dec((uint64_t)code);
    print("]\n");

    p->exited = 1;
    p->exit_code = code;
    p->reap_pending = 1;
}

void process_reap_deferred(void)
{
    Task* current = task_current();
    AddressSpace* current_as = current ? current->address_space : 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process* p = &proc_table[i];
        if (!p->used || !p->reap_pending)
            continue;

        if (!p->address_space) {
            p->reap_pending = 0;
            p->main_tid = -1;
            continue;
        }

        if (current_as == p->address_space)
            continue;

        vmm_destroy_address_space(p->address_space);
        p->address_space = 0;
        p->reap_pending = 0;
        p->main_tid = -1;
    }
}

void process_dump_table(void)
{
    print("proc: pid tid state exit\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process* p = &proc_table[i];
        if (!p->used) continue;

        print("proc: ");
        print_dec((uint64_t)p->pid);
        print(" ");
        print_dec((uint64_t)p->main_tid);
        print(" ");
        if (p->exited) print("EXITED");
        else print("RUNNING");
        print(" ");
        print_dec((uint64_t)p->exit_code);
        print("\n");
    }
}

int process_kill(int pid, int code)
{
    Process* p = find_process_by_pid(pid);
    if (!p || !p->used)
        return -1;

    if (p->exited)
        return 1;

    if (task_kill_pid(pid, code) < 0)
        return -1;

    p->exited = 1;
    p->exit_code = code;
    p->reap_pending = 1;
    return 0;
}

int process_wait_poll(int pid, int* out_code)
{
    Process* p = find_process_by_pid(pid);
    if (!p || !p->used)
        return -1;

    if (!p->exited)
        return 0;

    if (out_code)
        *out_code = p->exit_code;

    if (!p->reap_pending && p->address_space == 0) {
        p->used = 0;
        p->pid = 0;
        p->main_tid = -1;
        p->entry = 0;
        p->user_rsp = 0;
        p->exit_code = 0;
        p->exited = 0;
    }

    return 1;
}

int process_is_active(int pid)
{
    Process* p = find_process_by_pid(pid);
    if (!p || !p->used)
        return 0;
    return !p->exited;
}

uint64_t process_count_active(void)
{
    uint64_t n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (proc_table[i].used && !proc_table[i].exited)
            n++;
    return n;
}
