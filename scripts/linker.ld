ENTRY(_start)

SECTIONS
{
    . = 1M;
    
    _kernel_start = .;

    .text : ALIGN(4K)
    {
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(16);
        stack_bottom = .;
        . += 0x4000;  /* 16KB stack */
        stack_top = .;
    }
    
    _kernel_end = .;
}
