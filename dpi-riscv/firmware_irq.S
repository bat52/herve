/**
 * firmware_irq.S
 *
 * IRQ-driven GPIO toggle firmware for tb_top.
 *
 * MMIO register map:
 *   0x1000_0000  GPIO_OUT     — R/W  Output value (bit 0 = LED)
 *   0x1000_0004  GPIO_IE      — R/W  Interrupt enable
 *   0x1000_0008  GPIO_STATUS  — R    Interrupt status (bit 0 = IRQ pending)
 *
 * Flow:
 *   1. Configure mtvec to point to interrupt_handler
 *   2. Enable MIE in mstatus
 *   3. Enable GPIO interrupt (GPIO_IE = 1)
 *   4. Enter main loop (spin)
 *   5. On IRQ: handler reads GPIO_STATUS, toggles GPIO_OUT, mret
 *
 * Build:
 *   riscv64-unknown-elf-as -march=rv32im -o firmware_irq.o firmware_irq.S
 *   riscv64-unknown-elf-ld -Ttext=0x00000000 -o firmware_irq.elf firmware_irq.o
 *   riscv64-unknown-elf-objcopy -O binary firmware_irq.elf firmware_irq.bin
 */

.section .text
.globl _start

_start:
    /* t0 = MMIO_BASE (0x1000_0000) */
    lui t0, 0x10000                # t0 = 0x1000_0000

    /* ================================================================
     * 1. Configure mtvec to point to interrupt_handler
     *    Place handler at a known address (e.g. 0x100)
     * ================================================================ */
    lui t1, 0x00010                # t1 = 0x00010000 (too high)
    /* Instead, use a small address: handler at 0x100 */
    addi t1, x0, 0x100
    csrw mtvec, t1

    /* ================================================================
     * 2. Enable interrupts in mstatus (set MIE bit = bit 3)
     * ================================================================ */
    addi t1, x0, 0x8
    csrs mstatus, t1

    /* ================================================================
     * 3. Enable GPIO interrupt: GPIO_IE = 1
     * ================================================================ */
    addi t1, x0, 1
    sw   t1, 4(t0)                 # GPIO_IE = 1

    /* ================================================================
     * 4. Main loop: spin forever
     *    When IRQ fires, PC jumps to mtvec (interrupt_handler)
     *    After mret, execution resumes here
     * ================================================================ */
main_loop:
    j main_loop

    /* ================================================================
     * 5. Interrupt handler at address 0x100
     *    - Read GPIO_STATUS to clear IRQ
     *    - Read GPIO_OUT, toggle bit 0, write back
     *    - mret
     * ================================================================ */
.org 0x100
interrupt_handler:
    /* Read GPIO_STATUS (offset 0x08) — clears the IRQ status */
    lw   t1, 8(t0)                 # t1 = GPIO_STATUS

    /* Read GPIO_OUT (offset 0x00), toggle bit 0 */
    lw   t2, 0(t0)                 # t2 = GPIO_OUT
    xori t2, t2, 1                 # t2 = GPIO_OUT ^ 1
    sw   t2, 0(t0)                 # GPIO_OUT = toggled value

    /* Return from interrupt */
    mret
