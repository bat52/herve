/*
 * firmware_blinky_tz.S -- Blinky for tb_top_zephyr.sv (mem_write GPIO)
 *
 * Toggles the GPIO (mem_write) every TIMER_PERIOD clock cycles.
 * Uses tb_top_zephyr.sv's register map:
 *   0x1000_0000  GPIO_OUT   (R/W) via mem_write
 *   0x1000_0010  MTIME_LO   (R)
 *   0x1000_0018  MTIMECMP_LO (R/W)
 *   0x1000_001C  MTIMECMP_HI (R/W)
 */

.section .text
.globl _start

.equ GPIO_OUT,     0x00
.equ MTIME_LO,     0x10
.equ MTIMECMP_LO,  0x18
.equ MTIMECMP_HI,  0x1C
.equ TIMER_PERIOD, 50000
.equ STACK_TOP,  0x00400000

_start:
    /* Initialize stack pointer */
    lui  sp, (STACK_TOP >> 12)    # sp = 0x0040_0000

    lui t0, 0x10000               # t0 = 0x1000_0000

    /* Set mtimecmp = mtime + TIMER_PERIOD */
    lw   t1, MTIME_LO(t0)
    li   t2, TIMER_PERIOD
    add  t1, t1, t2
    sw   t1, MTIMECMP_LO(t0)
    sw   x0, MTIMECMP_HI(t0)

    /* Configure mtvec */
    la   t1, interrupt_handler
    csrw mtvec, t1

    /* Enable MTIE (bit 7) in mie */
    li   t1, 0x80
    csrw mie, t1

    /* Enable MIE (bit 3) in mstatus */
    li   t1, 0x08
    csrs mstatus, t1

main_loop:
    wfi
    j main_loop

.align 4
interrupt_handler:
    addi sp, sp, -8
    sw   t0, 0(sp)
    sw   t1, 4(sp)

    lui  t0, 0x10000

    /* Toggle GPIO_OUT (bit 0) */
    lw   t1, GPIO_OUT(t0)
    xori t1, t1, 1
    sw   t1, GPIO_OUT(t0)

    /* Schedule next tick */
    lw   t1, MTIME_LO(t0)
    li   t2, TIMER_PERIOD
    add  t1, t1, t2
    sw   t1, MTIMECMP_LO(t0)

    lw   t0, 0(sp)
    lw   t1, 4(sp)
    addi sp, sp, 8
    mret
