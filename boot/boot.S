.section .text
.global _start

_start:
    # Disable interrupts
    cli
    
    # Clear direction flag
    cld
    
    # Set up stack pointer
    mov $stack_top, %rsp
    
    # Align stack to 16 bytes (required by System V ABI)
    and $-16, %rsp
    
    # Zero the base pointer
    xor %rbp, %rbp
    
    # BootInfo pointer is already in %rdi (first argument)
    # Save it in a callee-saved register just in case
    mov %rdi, %rbx
    
    # Call kernel_main
    mov %rbx, %rdi
    call kernel_main
    
    # If kernel_main returns, halt
.halt:
    cli
    hlt
    jmp .halt