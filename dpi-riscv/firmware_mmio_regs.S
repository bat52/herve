/**
 * firmware_mmio_regs.S
 *
 * Test firmware for MMIO register configuration verification.
 * MMIO base: 0x1000_0000
 *
 * Writes four known values to MMIO register slots 0-3, reads them back,
 * then halts with ebreak. The SV testbench verifies the register values.
 *
 *   reg[0] = 0xAAAA_AAAB  (at offset 0x00)
 *   reg[1] = 0x0000_05A5  (at offset 0x04)
 *   reg[2] = 0x5555_5554  (at offset 0x08)
 *   reg[3] = 0x0000_07FF  (at offset 0x0C)
 *
 * Build:
 *   riscv64-unknown-elf-as -march=rv32im -o firmware_mmio_regs.o firmware_mmio_regs.S
 *   riscv64-unknown-elf-ld -Ttext=0x00000000 -o firmware_mmio_regs.elf firmware_mmio_regs.o
 *   riscv64-unknown-elf-objcopy -O binary firmware_mmio_regs.elf firmware_mmio_regs.bin
 */

.section .text
.globl _start

_start:
    /* t0 = MMIO_BASE (0x1000_0000) */
    lui t0, 0x10000                # t0 = 0x1000_0000

    /* ================================================================
     * Write 0xAAAA_AAAB to reg[0] (offset 0x00)
     *   0xAAAA_AAAB = 0xAAAA_B000 - 0x555
     *   Lower 12 bits = 0xAAB (sign-extends to -0x555 = -1365)
     *   lui 0xAAAA_B + addi -0x555 = 0xAAAA_B000 - 0x555 = 0xAAAA_AAAB
     * ================================================================ */
    lui  t1, 0xAAAAB               # t1 = 0xAAAA_B000
    addi t1, t1, -1365             # t1 = 0xAAAA_AAAB (0xAAB = -1365 signed)
    sw   t1, 0(t0)                 # reg[0] = 0xAAAA_AAAB

    /* ================================================================
     * Write 0x5A5 to reg[1] (offset 0x04)
     * ================================================================ */
    addi t1, x0, 0x5A5
    sw   t1, 4(t0)                 # reg[1] = 0x5A5

    /* ================================================================
     * Write 0x5555_5554 to reg[2] (offset 0x08)
     *   0x5555_5554 = 0x5555_5000 + 0x554
     *   Lower 12 bits = 0x554 (sign-extends to 0x554 = 1364)
     * ================================================================ */
    lui  t1, 0x55555               # t1 = 0x5555_5000
    addi t1, t1, 1364              # t1 = 0x5555_5554 (0x554 = 1364)
    sw   t1, 8(t0)                 # reg[2] = 0x5555_5554

    /* ================================================================
     * Write 0x7FF to reg[3] (offset 0x0C)
     * ================================================================ */
    addi t1, x0, 0x7FF
    sw   t1, 12(t0)                # reg[3] = 0x7FF

    /* Halt */
    ebreak
