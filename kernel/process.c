#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "display.h"

// ELF types
#define PT_LOAD   1
#define PF_W      2
#define PF_R      4

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

static uint64_t next_pid = 1;

Process* process_create_from_elf(void* elf_data, uint64_t size)
{
    (void)size;
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_data;

    // validate ELF
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        print("process: invalid ELF\n");
        return 0;
    }

    // allocate process struct
    Process* proc = (Process*)pmm_alloc_page();
    if (!proc) return 0;

    proc->pid           = next_pid++;
    proc->address_space = vmm_create_address_space();
    proc->entry         = ehdr->e_entry;

    // load segments
    Elf64_Phdr* phdrs = (Elf64_Phdr*)((uint8_t*)elf_data + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (ph->p_flags & PF_W) flags |= VMM_FLAG_WRITE;

        // map pages for this segment
        uint64_t vaddr = ph->p_vaddr & ~0xFFFULL;  // align down
        uint64_t vend  = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t va = vaddr; va < vend; va += 0x1000) {
            void* page = pmm_alloc_page();
            if (!page) {
                print("process: out of memory\n");
                return 0;
            }
            // zero the page
            uint8_t* p = (uint8_t*)page;
            for (int j = 0; j < 4096; j++) p[j] = 0;

            vmm_map(proc->address_space, va, (uint64_t)page, flags);
        }

        // copy segment data
        // src is in kernel memory (identity mapped)
        // dst virtual address — access through identity map of physical page
        uint8_t* src = (uint8_t*)elf_data + ph->p_offset;
        uint8_t* dst = (uint8_t*)ph->p_vaddr;

        // we need to copy via physical address
        // walk page tables to get physical address for each page
        for (uint64_t off = 0; off < ph->p_filesz; off++) {
            uint64_t va   = ph->p_vaddr + off;
            uint64_t phys = vmm_virt_to_phys(proc->address_space, va);
            uint64_t page_off = va & 0xFFF;
            ((uint8_t*)phys)[page_off] = src[off];
        }
    }

    // allocate user stack
    for (int i = 0; i < PROCESS_STACK_PAGES; i++) {
        void* page = pmm_alloc_page();
        uint8_t* p = (uint8_t*)page;
        for (int j = 0; j < 4096; j++) p[j] = 0;
        uint64_t va = PROCESS_STACK_VIRT - (uint64_t)(i + 1) * 0x1000;
        vmm_map(proc->address_space, va,
                (uint64_t)page,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    }

    proc->user_rsp = PROCESS_STACK_VIRT;

    print("process: created PID ");
    print_dec(proc->pid);
    print(" entry=");
    print_hex(proc->entry);
    print("\n");

    return proc;
}

void process_run(Process* p)
{
    // update TSS RSP0 with kernel stack for this process
    // (when this process makes a syscall, CPU switches to RSP0)
    extern void tss_set_rsp0(uint64_t rsp);
    // we reuse the percpu syscall stack — fine for now
    // (will be per-process stack in scheduler)

    // switch to process address space
    vmm_switch(p->address_space);

    // jump to ring 3
    // push user SS, user RSP, RFLAGS, user CS, user RIP
    // then iretq
    __asm__ volatile (
        "mov $0x23, %%ax\n"     // user data selector (0x20 | 3)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"

        "push $0x23\n"          // SS  = user data
        "push %0\n"             // RSP = user stack
        "pushf\n"               // RFLAGS
        "pop %%rax\n"
        "or $0x200, %%rax\n"    // set IF (enable interrupts in user mode)
        "push %%rax\n"
        "push $0x1B\n"          // CS  = user code (0x18 | 3)
        "push %1\n"             // RIP = entry point
        "iretq\n"
        :
        : "r"(p->user_rsp), "r"(p->entry)
        : "rax"
    );
}