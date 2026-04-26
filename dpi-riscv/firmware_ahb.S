/**
 * firmware_ahb.S
 *
 * AHB-Lite GPIO test firmware.
 *
 * Tests MMIO reads and writes through the AHB-Lite bus to the GPIO DUT.
 *
 * Register map (AHB GPIO at 0x1000_0000):
 *   0x1000_0000  GPIO_OUT     — R/W  Output value
 *   0x1000_0004  GPIO_IE      — R/W  Interrupt enable
 *   0x1000_0008  GPIO_STATUS  — R    Interrupt status
 *
 * Flow:
 *   1. Write 0xDEAD_BEEF to GPIO_OUT
 *   2. Write 0x00000001 to GPIO_IE
 *   3. Read back GPIO_OUT, verify it matches
 *   4. Read back GPIO_IE, verify it matches
 *   5. Read GPIO_STATUS (should be 0, no IRQ)
 *   6. Write 0x00000000 to GPIO_OUT (clear)
 *   7. Read back GPIO_OUT, verify it's 0
 *   8. ebreak
 *
 * Build:
 *   riscv64-unknown-elf-as -march=rv32im -o firmware_ahb.o firmware_ahb.S
 *   riscv64-unknown-elf-ld -Ttext=0x00000000 -o firmware_ahb.elf firmware_ahb.o
 *   riscv64-unknown-elf-objcopy -O binary firmware_ahb.elf firmware_ahb.bin
 */

.section .text
.globl _start

_start:
    /* t0 = MMIO_BASE (0x1000_0000) */
    lui t0, 0x10000                # t0 = 0x1000_0000

    /* ================================================================
     * 1. Write 0xDEAD_BEEF to GPIO_OUT (offset 0x00)
     * ================================================================ */
    /* Construct 0xDEAD_BEEF using lui + addi */
    lui  t1, 0xDEADC               # t1 = 0xDEAD_C000
    addi t1, t1, -273              # t1 = 0xDEAD_BEEF (sign-extended 0xEEF = -273)
    sw   t1, 0(t0)                 # GPIO_OUT = 0xDEAD_BEEF

    /* ================================================================
     * 2. Write 0x00000001 to GPIO_IE (offset 0x04)
     * ================================================================ */
    addi t1, x0, 1
    sw   t1, 4(t0)                 # GPIO_IE = 1

    /* ================================================================
     * 3. Read back GPIO_OUT, verify it matches
     * ================================================================ */
    lw   t2, 0(t0)                 # t2 = GPIO_OUT
    /* Reconstruct 0xDEAD_BEEF in t1 for comparison */
    lui  t1, 0xDEADC               # t1 = 0xDEAD_C000
    addi t1, t1, -273              # t1 = 0xDEAD_BEEF
    bne  t2, t1, fail              # if mismatch, jump to fail

    /* ================================================================
     * 4. Read back GPIO_IE, verify it matches
     * ================================================================ */
    lw   t2, 4(t0)                 # t2 = GPIO_IE
    addi t1, x0, 1
    bne  t2, t1, fail              # if mismatch, jump to fail

    /* ================================================================
     * 5. Read GPIO_STATUS (should be 0, no IRQ)
     * ================================================================ */
    lw   t2, 8(t0)                 # t2 = GPIO_STATUS
    bne  t2, x0, fail              # if non-zero, jump to fail

    /* ================================================================
     * 6. Write 0x00000000 to GPIO_OUT (clear)
     * ================================================================ */
    sw   x0, 0(t0)                 # GPIO_OUT = 0

    /* ================================================================
     * 7. Read back GPIO_OUT, verify it's 0
     * ================================================================ */
    lw   t2, 0(t0)                 # t2 = GPIO_OUT
    bne  t2, x0, fail              # if non-zero, jump to fail

    /* ================================================================
     * 8. Success: write 1 to MMIO_BASE + 0x100 (pass indicator)
     *    Then ebreak
     * ================================================================ */
    addi t1, x0, 1
    sw   t1, 0x100(t0)             # pass indicator at 0x1000_0100
    ebreak

fail:
    /* Write 0 to pass indicator and halt */
    sw   x0, 0x100(t0)             # fail indicator at 0x1000_0100
    ebreak
