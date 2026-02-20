.global syscall_entry

syscall_entry:
    # CPU saved: RCX=user RIP, R11=user RFLAGS
    # RSP is still user stack — dangerous, switch immediately

    swapgs                          # GS now points to PerCPU
    mov  %rsp, %gs:8               # save user RSP at offset 8
    mov  %gs:0, %rsp               # load kernel RSP from offset 0

    # build a proper stack frame
    push %r11                       # user RFLAGS
    push %rcx                       # user RIP
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    # syscall args: rdi=num, rsi=arg0, rdx=arg1, r10=arg2
    # C calling convention: rdi, rsi, rdx, rcx
    mov  %r10, %rcx                 # move arg2 into rcx for C ABI

    call syscall_handler

    # restore saved registers
    pop  %r15
    pop  %r14
    pop  %r13
    pop  %r12
    pop  %rbx
    pop  %rbp
    pop  %rcx                       # user RIP → rcx for sysretq
    pop  %r11                       # user RFLAGS → r11 for sysretq

    # restore user stack
    mov  %gs:8, %rsp

    swapgs                          # restore user GS
    sysretq                         # back to ring 3