.global syscall_entry
syscall_entry:
    # CPU saved: RCX=user RIP, R11=user RFLAGS
    # RSP is still user stack with user CR3 (SYSCALL doesn't change CR3!)
    # GS still has user GS base
    
    swapgs                         # GS now points to PerCPU (KERNEL_GS_BASE)
    
    # CRITICAL: CR3 is still user! Must switch to kernel CR3 BEFORE
    # accessing any kernel memory via the high-half addresses
    mov  %gs:16, %rax              # load kernel CR3 from percpu offset 16
    mov  %rax, %cr3                # switch to kernel address space
    
    mov  %rsp, %gs:8               # save user RSP at offset 8
    mov  %gs:0, %rsp               # load kernel RSP from offset 0

    push %r11
    push %rcx
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    mov  %r10, %rcx
    call syscall_handler

    pop  %r15
    pop  %r14
    pop  %r13
    pop  %r12
    pop  %rbx
    pop  %rbp
    pop  %rcx
    pop  %r11

    # restore user CR3 before sysretq
    mov  %gs:24, %rax              # user CR3 at offset 24
    mov  %rax, %cr3

    mov  %gs:8, %rsp               # restore user RSP
    swapgs
    sysretq