/**
 * rv32_dpi_test.c — Standalone ISS test for RV32M (MUL/DIV/REM) extension.
 *
 * This test builds RV32IM instructions programmatically in the ISS RAM,
 * executes them, and verifies results by reading back register values.
 * No Verilator, no SV, no RISC-V toolchain required.
 *
 * Compile:
 *   gcc -I. -o rv32_dpi_test rv32_dpi_test.c rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_test
 */

#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// -----------------------------------------------------------------------
// DPI stubs — these are called by the ISS for MMIO access.
// For this standalone test, we just use a simple memory buffer.
// -----------------------------------------------------------------------

static uint32_t mmio_region[64] = {0};

extern "C" int dpi_mmio_read(int addr) {
    if (addr >= 0x10000000 && addr < 0x10000100) {
        int idx = (addr - 0x10000000) / 4;
        return (int)mmio_region[idx];
    }
    return 0;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    if (addr >= 0x10000000 && addr < 0x10000100) {
        int idx = (addr - 0x10000000) / 4;
        mmio_region[idx] = (uint32_t)data;
    }
}

// -----------------------------------------------------------------------
// RISC-V instruction helpers (RV32I + M)
// -----------------------------------------------------------------------

static uint32_t make_addi(unsigned rd, unsigned rs1, int32_t imm) {
    return ((uint32_t)(imm & 0xfff) << 20) | (rs1 << 15) | (0 << 12) | (rd << 7) | 0x13u;
}

static uint32_t make_lui(unsigned rd, uint32_t imm20) {
    return (imm20 << 12) | (rd << 7) | 0x37u;
}

static uint32_t make_sw(unsigned rs2, unsigned rs1, int32_t imm) {
    uint32_t imm11_5 = ((uint32_t)imm >> 5) & 0x7fu;
    uint32_t imm4_0  = (uint32_t)imm & 0x1fu;
    return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (2u << 12) | (imm4_0 << 7) | 0x23u;
}

static uint32_t make_ebreak(void) {
    return 0x00100073u;
}

static uint32_t make_mul(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x0u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_mulh(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x1u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_mulhsu(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x2u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_mulhu(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x3u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_div(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x4u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_divu(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x5u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_rem(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x6u << 12) | (rd << 7) | 0x33u;
}

static uint32_t make_remu(unsigned rd, unsigned rs1, unsigned rs2) {
    return (0x01u << 25) | (rs2 << 20) | (rs1 << 15) | (0x7u << 12) | (rd << 7) | 0x33u;
}

static void load_imm(uint32_t *ram, unsigned *addr, unsigned rd, uint32_t value) {
    uint32_t upper = value >> 12;
    uint32_t lower = value & 0xfff;
    if (lower & 0x800) {
        upper += 1;
        lower = (int32_t)(int16_t)(lower | 0xfffff000);
        ram[(*addr)++] = make_lui(rd, upper);
        ram[(*addr)++] = make_addi(rd, rd, (int32_t)lower);
    } else {
        ram[(*addr)++] = make_lui(rd, upper);
        ram[(*addr)++] = make_addi(rd, rd, (int32_t)lower);
    }
}

// =======================================================================
// Test infrastructure
// =======================================================================

#define MMIO_BASE_REG 5
#define OP1_REG 6
#define OP2_REG 7
#define RES_REG 28

struct TestCase {
    unsigned reg_idx;
    const char *name;
    uint32_t expected;
};

static const TestCase test_cases[] = {
    { 0,  "MUL: 7 * 3",                             21u              },
    { 1,  "MULH: 0x12345678^2 upper",                0x014B66DCu      },
    { 2,  "MULHSU: -1000 * 2000000 upper",           0xFFFFFFFFu      },
    { 3,  "MULHU: 0xFFFFFFFF^2 upper",               0xFFFFFFFEu      },
    { 4,  "DIV: 100 / 3",                            33u              },
    { 5,  "DIVU: 200 / 7",                           28u              },
    { 6,  "REM: 100 %% 3",                            1u               },
    { 7,  "REMU: 200 %% 7",                           4u               },
    { 8,  "DIV by zero",                             0xFFFFFFFFu      },
    { 9,  "DIVU by zero",                            0xFFFFFFFFu      },
    { 10, "REM by zero",                             50u              },
    { 11, "REMU by zero",                            50u              },
    { 12, "INT32_MIN / -1 (overflow)",               (uint32_t)INT32_MIN },
    { 13, "INT32_MIN %% -1 (overflow)",               0u               },
    { 14, "MUL: 0x10000001^2 low",                   0x20000001u      },
    { 15, "MULH: 0x10000001^2 upper",                0x01000000u      },
};

static const unsigned num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

static void build_firmware(uint32_t *ram) {
    unsigned a = 0;

    // Prologue: set MMIO base
    ram[a++] = make_lui(MMIO_BASE_REG, 0x10000u);
    ram[a++] = make_addi(MMIO_BASE_REG, MMIO_BASE_REG, 0);

    // Test 0: MUL 7 * 3 = 21
    ram[a++] = make_addi(OP1_REG, 0, 7);
    ram[a++] = make_addi(OP2_REG, 0, 3);
    ram[a++] = make_mul(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 0);

    // Test 1: MULH 0x12345678^2 upper = 0x014B66DC
    load_imm(ram, &a, OP1_REG, 0x12345678u);
    ram[a++] = make_mulh(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 4);

    // Test 2: MULHSU -1000 * 2000000 upper = 0xFFFFFFFF
    ram[a++] = make_addi(OP1_REG, 0, -1000);
    load_imm(ram, &a, OP2_REG, 2000000u);
    ram[a++] = make_mulhsu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 8);

    // Test 3: MULHU 0xFFFFFFFF^2 upper = 0xFFFFFFFE
    ram[a++] = make_addi(OP1_REG, 0, -1);
    ram[a++] = make_mulhu(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 12);

    // Test 4: DIV 100 / 3 = 33
    ram[a++] = make_addi(OP1_REG, 0, 100);
    ram[a++] = make_addi(OP2_REG, 0, 3);
    ram[a++] = make_div(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 16);

    // Test 5: DIVU 200 / 7 = 28
    ram[a++] = make_addi(OP1_REG, 0, 200);
    ram[a++] = make_addi(OP2_REG, 0, 7);
    ram[a++] = make_divu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 20);

    // Test 6: REM 100 % 3 = 1
    ram[a++] = make_addi(OP1_REG, 0, 100);
    ram[a++] = make_addi(OP2_REG, 0, 3);
    ram[a++] = make_rem(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 24);

    // Test 7: REMU 200 % 7 = 4
    ram[a++] = make_addi(OP1_REG, 0, 200);
    ram[a++] = make_addi(OP2_REG, 0, 7);
    ram[a++] = make_remu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 28);

    // Test 8: DIV by zero -> 0xFFFFFFFF
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_div(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 32);

    // Test 9: DIVU by zero -> 0xFFFFFFFF
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_divu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 36);

    // Test 10: REM by zero -> 50 (dividend)
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_rem(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 40);

    // Test 11: REMU by zero -> 50 (dividend)
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_remu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 44);

    // Test 12: INT32_MIN / -1 -> INT32_MIN (overflow)
    load_imm(ram, &a, OP1_REG, (uint32_t)INT32_MIN);
    ram[a++] = make_addi(OP2_REG, 0, -1);
    ram[a++] = make_div(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 48);

    // Test 13: INT32_MIN % -1 -> 0 (overflow)
    load_imm(ram, &a, OP1_REG, (uint32_t)INT32_MIN);
    ram[a++] = make_addi(OP2_REG, 0, -1);
    ram[a++] = make_rem(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 52);

    // Test 14: MUL 0x10000001^2 low = 0x20000001
    load_imm(ram, &a, OP1_REG, 0x10000001u);
    ram[a++] = make_mul(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 56);

    // Test 15: MULH 0x10000001^2 upper = 0x01000000
    ram[a++] = make_mulh(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 60);

    // Epilogue: ebreak
    ram[a] = make_ebreak();
}

// =======================================================================
// Main
// =======================================================================
int main(void) {
    printf("=== RV32M (MUL/DIV/REM) Extension Test (Standalone) ===\n\n");

    // Initialize ISS
    rv_init(NULL, 1 << 20);
    uint32_t *ram = (uint32_t *)rv_get_ram();

    // Build firmware
    build_firmware(ram);

    // Reset
    rv_reset(0);

    // Execute
    int executed = rv_step(200);
    printf("Executed %d instructions\n\n", executed);

    // Verify results by reading MMIO region
    unsigned pass_count = 0;
    unsigned fail_count = 0;
    for (unsigned i = 0; i < num_tests; i++) {
        uint32_t actual = mmio_region[test_cases[i].reg_idx];
        if (actual == test_cases[i].expected) {
            printf("  [PASS] %s = 0x%08x\n", test_cases[i].name, actual);
            pass_count++;
        } else {
            printf("  [FAIL] %s = 0x%08x (expected 0x%08x)\n",
                   test_cases[i].name, actual, test_cases[i].expected);
            fail_count++;
        }
    }

    printf("\n===========================\n");
    printf("  Passed: %u / %u\n", pass_count, num_tests);
    printf("  Failed: %u / %u\n", fail_count, num_tests);
    printf("===========================\n");

    return (fail_count > 0) ? 1 : 0;
}
