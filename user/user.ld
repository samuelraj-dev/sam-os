ENTRY(_start)

SECTIONS
{
    . = 0x0000002000000000;    /* userspace VA outside low identity-map huge pages */

    .text : {
        *(.text*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)
    }

    .bss : ALIGN(4K) {
        *(.bss*)
        *(COMMON)
    }
}
