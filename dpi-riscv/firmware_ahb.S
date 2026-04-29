/**
 * firmware_ahb.S
 *
 * AHB-Lite GPIO IRQ self-test firmware.
 *
 * Tests the full IRQ injection path:
 *   1. Configure mtvec to point to interrupt handler (at 0x100)
 *   2. Enable MIE in mstatus
 *   3. Enable GPIO interrupt (GPIO_IE = 1)
 *   4. Write GPIO_OUT = 1 (bit 0 drives ext_irq in the Verilog testbench)
 *   5. Poll GPIO_OUT in a loop — when handler clears it, fall through
 *   6. Verilog detects ext_irq, calls rv_set_irq(1) on posedge clk
 *   7. Next rv_step() — ISS vectors to interrupt handler at 0x100
 *   8. Handler reads GPIO_STATUS, clears GPIO_OUT (bit 0 = 0), mret
 *   9. Back in wait loop: reads GPIO_OUT == 0, exits loop
 *   10. Write 1 to pass indicator at 0x1000_0100, ebreak
 *
 * Register map (AHB GPIO at 0x1000_0000):
 *   0x1000_0000  GPIO_OUT     — R/W  Output value (bit 0 = ext_irq)
 *   0x1000_0004  GPIO_IE      — R/W  Interrupt enable
 *   0x1000_0008  GPIO_STATUS  — R    Interrupt status (bit 0 = ext_irq)
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
     * 1. Configure mtvec to point to interrupt_handler at 0x100
     * ================================================================ */
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
     * 4. Write GPIO_OUT = 1 — this drives ext_irq high via gpio_out[0]
     * ================================================================ */
    addi t1, x0, 1
    sw   t1, 0(t0)                 # GPIO_OUT = 1 → ext_irq = 1

    /* ================================================================
     * 5. Poll GPIO_OUT in a loop until the interrupt handler clears it.
     *    The IRQ fires, rv_step vectors to handler at 0x100,
     *    handler clears GPIO_OUT and mret, execution resumes here,
     *    the load (lw) sees GPIO_OUT == 0, loop exits.
     * ================================================================ */
wait_loop:
    lw   t2, 0(t0)                 # t2 = GPIO_OUT
    bne  t2, x0, wait_loop         # loop while GPIO_OUT != 0

    /* ================================================================
     * 6. GPIO_OUT == 0 — write pass indicator and ebreak
     * ================================================================ */
    addi t1, x0, 1
    sw   t1, 0x100(t0)             # pass indicator at 0x1000_0100
    ebreak

    /* ================================================================
     * Interrupt handler at address 0x100
     *    - Read GPIO_STATUS (acknowledge the IRQ)
     *    - Write GPIO_OUT = 0 (clear ext_irq)
     *    - mret
     * ================================================================ */
.org 0x100
interrupt_handler:
    /* Read GPIO_STATUS (offset 0x08) — acknowledges IRQ */
    lw   t1, 8(t0)                 # t1 = GPIO_STATUS

    /* Write GPIO_OUT = 0 to clear the interrupt line */
    sw   x0, 0(t0)                 # GPIO_OUT = 0 → ext_irq = 0

    /* Return from interrupt — resumes at wait_loop */
    mret
