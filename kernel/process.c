#include "process.h"
#include "percpu.h"
#include "pmm.h"
#include "vmm.h"
#include "paging.h"
#include "display.h"

// ELF types
#define PT_LOAD   1
#define PF_W      2
#define PF_R      4

#define PHYS_TO_VIRT(x) ((void*)((uint64_t)(x) + KERNEL_BASE))

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
    if (!elf_data || size < sizeof(Elf64_Ehdr)) {
        print("process: ELF buffer too small\n");
        return 0;
    }

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_data;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        print("process: invalid ELF\n");
        return 0;
    }

    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr) > size) {
        print("process: program headers outside ELF buffer\n");
        return 0;
    }

    Process* proc = (Process*)pmm_alloc_page();
    if (!proc) return 0;

    proc->pid           = next_pid++;
    proc->address_space = vmm_create_address_space();
    if (!proc->address_space) return 0;
    proc->entry = ehdr->e_entry;

    Elf64_Phdr* phdrs = (Elf64_Phdr*)((uint8_t*)elf_data + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_offset + ph->p_filesz > size) {
            print("process: segment outside ELF buffer\n");
            return 0;
        }

        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (ph->p_flags & PF_W) flags |= VMM_FLAG_WRITE;
        if (!(ph->p_flags & 0x1)) flags |= VMM_FLAG_NX;

        uint64_t vaddr = ph->p_vaddr & ~0xFFFULL;
        uint64_t vend  = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t va = vaddr; va < vend; va += 0x1000) {
            void* page = pmm_alloc_page();
            if (!page) {
                print("process: out of memory at va=");
                print_hex(va); print("\n");
                return 0;
            }
            // page is a physical address, but identity-mapped via pml4[0]
            // so we can access it directly as a virtual address
            uint8_t* vpage = (uint8_t*)page;
            for (int j = 0; j < 4096; j++) vpage[j] = 0;
            vmm_map(proc->address_space, va, (uint64_t)page, flags);
        }

        // copy data byte by byte via physical address
        uint8_t* src = (uint8_t*)elf_data + ph->p_offset;
        for (uint64_t off = 0; off < ph->p_filesz; off++) {
            uint64_t va   = ph->p_vaddr + off;
            uint64_t phys = vmm_virt_to_phys(proc->address_space, va);
            if (!phys) {
                print("process: virt_to_phys failed for va=");
                print_hex(va); print("\n");
                return 0;
            }
            // phys is physical address, identity-mapped via pml4[0]
            uint8_t* vaddr_kernel = (uint8_t*)(phys);
            vaddr_kernel[0] = src[off];
        }
    }

    for (int i = 0; i < PROCESS_STACK_PAGES; i++) {
        void* page = pmm_alloc_page();
        if (!page) return 0;
        // page is physical address, identity-mapped via pml4[0]
        uint8_t* vpage = (uint8_t*)page;
        for (int j = 0; j < 4096; j++) vpage[j] = 0;
        uint64_t va = PROCESS_STACK_VIRT - (uint64_t)(i + 1) * 0x1000;
        vmm_map(proc->address_space, va,
                (uint64_t)page,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    }

    proc->user_rsp = PROCESS_STACK_VIRT;
    print("process: created PID "); print_dec(proc->pid); print("\n");
    return proc;
}

// void process_run(Process* p)
// {
//     uint64_t entry   = p->entry;
//     uint64_t user_sp = p->user_rsp;

//     vmm_switch(p->address_space);

//     print("s");

//     __asm__ volatile (
//         // set user data segments
//         "mov $0x23, %%rax\n"
//         "mov %%rax, %%ds\n"
//         "mov %%rax, %%es\n"
//         "mov %%rax, %%fs\n"
//         "mov %%rax, %%gs\n"

//         // build iretq frame on stack:
//         // SS, RSP, RFLAGS, CS, RIP
//         "push $0x23\n"          // SS  = user data (0x20 | 3)
//         "push %[rsp]\n"         // RSP = user stack
//         "push $0x202\n"         // RFLAGS = IF set + reserved bit
//         "push $0x1B\n"          // CS  = user code (0x18 | 3)
//         "push %[rip]\n"         // RIP = entry point
//         "iretq\n"
//         :
//         : [rip]"r"(entry), [rsp]"r"(user_sp)
//         : "rax"
//     );
// }
void process_run(Process* p)
{
    extern PerCPU percpu_data;
    uint64_t user_cr3 = (uint64_t)p->address_space->pml4;
    percpu_data.user_cr3 = user_cr3;

    uint64_t entry   = p->entry;
    uint64_t user_sp = p->user_rsp;

    print("[process_run] launching user code at "); print_hex(entry); 
    print(" with stack at "); print_hex(user_sp); 
    print(" cr3="); print_hex(user_cr3); print("\n"); 
    display_flush();

    // Use iretq which properly handles all aspects of ring transition
    // Build frame on kernel stack, then switch CR3 and iretq
    __asm__ volatile (
        // Build iretq frame on kernel stack:
        // This frame will be popped after CR3 switch
        // Order: RIP, CS, RFLAGS, RSP, SS
        "push $0x23\n"          // SS  = user data (0x20 | 3)
        "push %[rsp]\n"         // RSP = user stack pointer
        "push $0x202\n"         // RFLAGS = IF set, reserved bit
        "push $0x1B\n"          // CS  = user code (0x18 | 3)
        "push %[rip]\n"         // RIP = entry point
        
        // Now switch CR3
        "mov %[cr3], %%rax\n"
        "mov %%rax, %%cr3\n"
        
        // iretq will:
        // 1. Pop RIP
        // 2. Pop CS (changes to user mode)
        // 3. Pop RFLAGS
        // 4. Pop RSP (sets user stack)
        // 5. Pop SS
        // 6. Jump to user code
        "iretq\n"
        :
        : [cr3]"r"(user_cr3), [rip]"r"(entry), [rsp]"r"(user_sp)
        : "rax", "memory"
    );
    
    // Never reached
}
