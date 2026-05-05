/**
 * firmware_muldiv.S
 *
 * Test firmware for RV32M (MUL/DIV/REM) extension verification.
 * MMIO base: 0x1000_0000
 *
 * Each test writes its result to a dedicated MMIO register slot:
 *   reg[0]  = MUL result
 *   reg[1]  = MULH result
 *   reg[2]  = MULHSU result
 *   reg[3]  = MULHU result
 *   reg[4]  = DIV result
 *   reg[5]  = DIVU result
 *   reg[6]  = REM result
 *   reg[7]  = REMU result
 *   reg[8]  = DIV by zero result
 *   reg[9]  = DIVU by zero result
 *   reg[10] = REM by zero result
 *   reg[11] = REMU by zero result
 *   reg[12] = INT32_MIN / -1 (DIV overflow)
 *   reg[13] = INT32_MIN % -1 (REM overflow)
 *   reg[14] = MUL large values
 *   reg[15] = MULH large values
 *
 * Build:
 *   riscv64-unknown-elf-as -march=rv32im -o firmware_muldiv.o firmware_muldiv.S
 *   riscv64-unknown-elf-ld -Ttext=0x00000000 -o firmware_muldiv.elf firmware_muldiv.o
 *   riscv64-unknown-elf-objcopy -O binary firmware_muldiv.elf firmware_muldiv.bin
 */

.section .text
.globl _start

_start:
    /* t0 = MMIO_BASE (0x1000_0000) */
    lui t0, 0x10000                # t0 = 0x1000_0000

    /* ================================================================
     * Test 1: MUL — basic multiplication
     *   7 * 3 = 21
     * ================================================================ */
    addi t1, x0, 7
    addi t2, x0, 3
    mul  t3, t1, t2                # t3 = 7 * 3 = 21
    sw   t3, 0(t0)                 # reg[0] = 21

    /* ================================================================
     * Test 2: MULH — signed upper 32 bits of 64-bit product
     *   0x12345678 * 0x12345678
     *   Full 64-bit result = 0x014B66DC_1DDF0D40
     *   Upper 32 bits = 0x014B66DC
     * ================================================================ */
    lui  t1, 0x12345
    addi t1, t1, 0x678             # t1 = 0x12345678
    mulh t3, t1, t1                # t3 = upper 32 bits (signed)
    sw   t3, 4(t0)                 # reg[1] = MULH result

    /* ================================================================
     * Test 3: MULHSU — signed * unsigned upper 32 bits
     *   signed(-1000) * unsigned(2000000)
     *   = -2000000000 -> upper bits
     * ================================================================ */
    addi t1, x0, -1000             # t1 = -1000 (signed)
    lui  t2, 0x1E8                 # t2 = 2000000 (unsigned) = 0x001E8480
    addi t2, t2, 0x480
    mulhsu t3, t1, t2              # t3 = upper 32 bits (signed*unsigned)
    sw   t3, 8(t0)                 # reg[2] = MULHSU result

    /* ================================================================
     * Test 4: MULHU — unsigned upper 32 bits
     *   0xFFFFFFFF * 0xFFFFFFFF
     *   Full 64-bit = 0xFFFFFFFE_00000001
     *   Upper 32 bits = 0xFFFFFFFE
     * ================================================================ */
    addi t1, x0, -1                # t1 = 0xFFFFFFFF (unsigned)
    mulhu t3, t1, t1               # t3 = upper 32 bits (unsigned)
    sw   t3, 12(t0)                # reg[3] = MULHU result

    /* ================================================================
     * Test 5: DIV — signed division
     *   100 / 3 = 33
     * ================================================================ */
    addi t1, x0, 100
    addi t2, x0, 3
    div  t3, t1, t2                # t3 = 100 / 3 = 33
    sw   t3, 16(t0)                # reg[4] = DIV result

    /* ================================================================
     * Test 6: DIVU — unsigned division
     *   200 / 7 = 28
     * ================================================================ */
    addi t1, x0, 200
    addi t2, x0, 7
    divu t3, t1, t2                # t3 = 200 / 7 = 28
    sw   t3, 20(t0)                # reg[5] = DIVU result

    /* ================================================================
     * Test 7: REM — signed remainder
     *   100 % 3 = 1
     * ================================================================ */
    addi t1, x0, 100
    addi t2, x0, 3
    rem  t3, t1, t2                # t3 = 100 % 3 = 1
    sw   t3, 24(t0)                # reg[6] = REM result

    /* ================================================================
     * Test 8: REMU — unsigned remainder
     *   200 % 7 = 4
     * ================================================================ */
    addi t1, x0, 200
    addi t2, x0, 7
    remu t3, t1, t2                # t3 = 200 % 7 = 4
    sw   t3, 28(t0)                # reg[7] = REMU result

    /* ================================================================
     * Test 9: DIV by zero — should return 0xFFFFFFFF
     * ================================================================ */
    addi t1, x0, 50
    addi t2, x0, 0
    div  t3, t1, t2                # t3 = 0xFFFFFFFF (div by zero)
    sw   t3, 32(t0)                # reg[8] = DIV by zero

    /* ================================================================
     * Test 10: DIVU by zero — should return 0xFFFFFFFF
     * ================================================================ */
    addi t1, x0, 50
    addi t2, x0, 0
    divu t3, t1, t2                # t3 = 0xFFFFFFFF (divu by zero)
    sw   t3, 36(t0)                # reg[9] = DIVU by zero

    /* ================================================================
     * Test 11: REM by zero — should return dividend (50)
     * ================================================================ */
    addi t1, x0, 50
    addi t2, x0, 0
    rem  t3, t1, t2                # t3 = 50 (rem by zero = dividend)
    sw   t3, 40(t0)                # reg[10] = REM by zero

    /* ================================================================
     * Test 12: REMU by zero — should return dividend (50)
     * ================================================================ */
    addi t1, x0, 50
    addi t2, x0, 0
    remu t3, t1, t2                # t3 = 50 (remu by zero = dividend)
    sw   t3, 44(t0)                # reg[11] = REMU by zero

    /* ================================================================
     * Test 13: INT32_MIN / -1 — overflow case, should return INT32_MIN
     * ================================================================ */
    lui  t1, 0x80000               # t1 = 0x80000000 = INT32_MIN
    addi t2, x0, -1
    div  t3, t1, t2                # t3 = INT32_MIN (overflow)
    sw   t3, 48(t0)                # reg[12] = INT32_MIN / -1

    /* ================================================================
     * Test 14: INT32_MIN % -1 — overflow case, should return 0
     * ================================================================ */
    lui  t1, 0x80000               # t1 = INT32_MIN
    addi t2, x0, -1
    rem  t3, t1, t2                # t3 = 0 (overflow remainder)
    sw   t3, 52(t0)                # reg[13] = INT32_MIN % -1

    /* ================================================================
     * Test 15: MUL large values
     *   0x10000001 * 0x10000001 = 0x01000000_20000001
     *   Low 32 bits = 0x20000001
     * ================================================================ */
    lui  t1, 0x10000
    addi t1, t1, 1                 # t1 = 0x10000001
    mul  t3, t1, t1                # t3 = low 32 bits
    sw   t3, 56(t0)                # reg[14] = MUL large

    /* ================================================================
     * Test 16: MULH large values
     *   0x10000001 * 0x10000001 = 0x01000000_20000001
     *   Upper 32 bits = 0x01000000
     * ================================================================ */
    mulh t3, t1, t1                # t3 = upper 32 bits (signed)
    sw   t3, 60(t0)                # reg[15] = MULH large

    /* Halt */
    ebreak
