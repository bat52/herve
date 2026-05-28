/*
 * firmware_timer.S
 *
 * Machine timer interrupt firmware for tb_top_zephyr.
 *
 * MMIO register map:
 *   0x1000_0000  GPIO_OUT     — R/W  Output value (bit 0 = LED)
 *   0x1000_0018  MTIMECMP_LO  — R/W  Lower 32 bits of mtimecmp
 *   0x1000_001C  MTIMECMP_HI  — R/W  Upper 32 bits of mtimecmp
 *
 * Flow:
 *   1. Set mtimecmp = 50000 (fire after ~50000 clock cycles)
 *   2. Enable MTIE in mie (bit 7)
 *   3. Configure mtvec to point to interrupt_handler
 *   4. Enable MIE in mstatus
 *   5. Enter main loop (WFI)
 *   6. On timer IRQ: handler toggles GPIO_OUT, sets new mtimecmp, mret
 *
 * Build:
 *   riscv64-unknown-elf-as -march=rv32im_zicsr -o firmware_timer.o firmware_timer.S
 *   riscv64-unknown-elf-ld -Ttext=0x00000000 -o firmware_timer.elf firmware_timer.o
 *   riscv64-unknown-elf-objcopy -O binary firmware_timer.elf firmware_timer.bin
 */

.section .text
.globl _start

_start:
    /* t0 = MMIO_BASE (0x1000_0000) */
    lui t0, 0x10000                # t0 = 0x1000_0000

    /* ================================================================
     * 1. Set mtimecmp = 50000 (0xC350)
     *    MTIMECMP_LO at offset 0x18, MTIMECMP_HI at offset 0x1C
     * ================================================================ */
    li   t1, 0xC350                # t1 = 50000
    sw   t1, 0x18(t0)              # MTIMECMP_LO = 50000
    sw   x0, 0x1C(t0)              # MTIMECMP_HI = 0

    /* ================================================================
     * 2. Enable MTIE (bit 7) in mie CSR (0x304)
     * ================================================================ */
    li   t1, 0x80                  # MTIE bit
    csrw mie, t1

    /* ================================================================
     * 3. Configure mtvec to point to interrupt_handler at 0x100
     * ================================================================ */
    li   t1, 0x100
    csrw mtvec, t1

    /* ================================================================
     * 4. Enable interrupts in mstatus (set MIE bit = bit 3)
     * ================================================================ */
    li   t1, 0x8
    csrs mstatus, t1

    /* ================================================================
     * 5. Main loop: wait for timer interrupt (WFI)
     * ================================================================ */
main_loop:
    wfi
    j main_loop

    /* ================================================================
     * 6. Interrupt handler at address 0x100
     *    - Toggle GPIO_OUT (bit 0)
     *    - Set new mtimecmp = mtime + 50000 (for periodic tick)
     *    - mret
     * ================================================================ */
.org 0x100
interrupt_handler:
    /* Read GPIO_OUT (offset 0x00), toggle bit 0 */
    lw   t2, 0(t0)                 # t2 = GPIO_OUT
    xori t2, t2, 1                 # t2 = GPIO_OUT ^ 1
    sw   t2, 0(t0)                 # GPIO_OUT = toggled value

    /* Read current MTIME_LO and add 50000 for next compare */
    lw   t2, 0x10(t0)              # t2 = MTIME_LO
    li   t3, 50000                 # t3 = 50000
    add  t2, t2, t3                # t2 = MTIME_LO + 50000
    sw   t2, 0x18(t0)              # MTIMECMP_LO = next fire time
    /* Assume no MTIME_HI overflow for short test */

    /* Return from interrupt */
    mret
