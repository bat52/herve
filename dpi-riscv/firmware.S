/**
 * firmware.S
 *
 * Simple test firmware for tb_top.
 * MMIO base: 0x1000_0000
 *
 * Writes a sequence of characters to the MMIO UART at 0x1000_0000,
 * then halts with ebreak.
 *
 * Build:
 *   riscv64-unknown-elf-as -march=rv32im -o firmware.o firmware.S
 *   riscv64-unknown-elf-ld -Ttext=0x00000000 -o firmware.elf firmware.o
 *   riscv64-unknown-elf-objcopy -O binary firmware.elf firmware.bin
 */

.section .text
.globl _start

_start:
    /* t0 = MMIO_BASE (0x1000_0000) */
    lui t0, 0x10000                # t0 = 0x1000_0000

    /* Write "Hello!" to MMIO UART */
    /* 'H' = 0x48 */
    addi t1, x0, 0x48
    sw   t1, 0(t0)

    /* 'e' = 0x65 */
    addi t1, x0, 0x65
    sw   t1, 0(t0)

    /* 'l' = 0x6C */
    addi t1, x0, 0x6C
    sw   t1, 0(t0)

    /* 'l' = 0x6C */
    sw   t1, 0(t0)

    /* 'o' = 0x6F */
    addi t1, x0, 0x6F
    sw   t1, 0(t0)

    /* '!' = 0x21 */
    addi t1, x0, 0x21
    sw   t1, 0(t0)

    /* '\n' = 0x0A */
    addi t1, x0, 0x0A
    sw   t1, 0(t0)

    /* Write pattern 0xDEAD_BEEF to MMIO */
    /* Construct 0xDEAD_BEEF using lui + addi */
    /* Lower 12 bits = 0xEEF (>= 0x800, so it sign-extends to -273) */
    /* Upper 20 bits = (0xDEAD_BEEF >> 12) + 1 = 0xDEADC */
    lui  t1, 0xDEADC               # t1 = 0xDEAD_C000
    addi t1, t1, -273              # t1 = 0xDEAD_BEEF (sign-extended 0xEEF = -273)
    sw   t1, 4(t0)                 # store to MMIO_BASE + 4

    /* Store 0x12345678 */
    lui  t1, 0x12345               # 0x12345000
    addi t1, t1, 0x678             # 0x12345678
    sw   t1, 8(t0)                 # store to MMIO_BASE + 8

    /* Halt */
    ebreak

