.section .text
.global _start

_start:
    cli # Disable interrupts
    cld # Clear direction flag
    mov $stack_top, %rsp # Set up stack pointer
    and $-16, %rsp # Align stack to 16 bytes (required by System V ABI)
    xor %rbp, %rbp # Zero the base pointer
    
    # BootInfo pointer is already in %rdi (first argument)
    mov %rdi, %rbx # Save it in a callee-saved register just in case
    mov %rbx, %rdi # Call kernel_main
    call kernel_main
.halt: # If kernel_main returns, halt
    cli
    hlt
    jmp .halt