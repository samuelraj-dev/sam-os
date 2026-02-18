.code64
.section .text

.global isr0
isr0:   pushq $0; pushq $0;  jmp isr_common
.global isr1
isr1:   pushq $0; pushq $1;  jmp isr_common
.global isr2
isr2:   pushq $0; pushq $2;  jmp isr_common
.global isr3
isr3:   pushq $0; pushq $3;  jmp isr_common
.global isr4
isr4:   pushq $0; pushq $4;  jmp isr_common
.global isr5
isr5:   pushq $0; pushq $5;  jmp isr_common
.global isr6
isr6:   pushq $0; pushq $6;  jmp isr_common
.global isr7
isr7:   pushq $0; pushq $7;  jmp isr_common
.global isr8          /* double fault - error code pushed by CPU */
isr8:   pushq $8;             jmp isr_common
.global isr9
isr9:   pushq $0; pushq $9;  jmp isr_common
.global isr10         /* error code pushed by CPU */
isr10:  pushq $10;            jmp isr_common
.global isr11         /* error code pushed by CPU */
isr11:  pushq $11;            jmp isr_common
.global isr12         /* error code pushed by CPU */
isr12:  pushq $12;            jmp isr_common
.global isr13         /* error code pushed by CPU */
isr13:  pushq $13;            jmp isr_common
.global isr14         /* error code pushed by CPU */
isr14:  pushq $14;            jmp isr_common
.global isr15
isr15:  pushq $0; pushq $15; jmp isr_common
.global isr16
isr16:  pushq $0; pushq $16; jmp isr_common
.global isr17         /* error code pushed by CPU */
isr17:  pushq $17;            jmp isr_common
.global isr18
isr18:  pushq $0; pushq $18; jmp isr_common
.global isr19
isr19:  pushq $0; pushq $19; jmp isr_common
.global isr20
isr20:  pushq $0; pushq $20; jmp isr_common

/* IRQs - no error codes */
.global isr32
isr32:  pushq $0; pushq $32; jmp isr_common
.global isr33
isr33:  pushq $0; pushq $33; jmp isr_common
.global isr34
isr34:  pushq $0; pushq $34; jmp isr_common
.global isr35
isr35:  pushq $0; pushq $35; jmp isr_common
.global isr36
isr36:  pushq $0; pushq $36; jmp isr_common
.global isr37
isr37:  pushq $0; pushq $37; jmp isr_common
.global isr38
isr38:  pushq $0; pushq $38; jmp isr_common
.global isr39
isr39:  pushq $0; pushq $39; jmp isr_common
.global isr40
isr40:  pushq $0; pushq $40; jmp isr_common
.global isr41
isr41:  pushq $0; pushq $41; jmp isr_common
.global isr42
isr42:  pushq $0; pushq $42; jmp isr_common
.global isr43
isr43:  pushq $0; pushq $43; jmp isr_common
.global isr44
isr44:  pushq $0; pushq $44; jmp isr_common
.global isr45
isr45:  pushq $0; pushq $45; jmp isr_common
.global isr46
isr46:  pushq $0; pushq $46; jmp isr_common
.global isr47
isr47:  pushq $0; pushq $47; jmp isr_common

.extern interrupt_handler
isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call interrupt_handler

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq