MEMORY { RAM (rwxa) : ORIGIN = 0x00000000, LENGTH = 16M }
SECTIONS {
    .text : { *(.text) *(.text.*) } > RAM
    .rodata : { *(.rodata) *(.rodata.*) } > RAM
    .data : { *(.data) *(.data.*) } > RAM
    .bss : { *(.bss) *(.bss.*) *(COMMON) } > RAM
    .stack : { . = ALIGN(16); . = . + 0x1000; _stack_top = .; } > RAM
}
ENTRY(_start)
__stack_top = _stack_top;
