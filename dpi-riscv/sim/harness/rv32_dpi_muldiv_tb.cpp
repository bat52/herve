/**
 * Simulation testcase: RV32M (MUL/DIV/REM) extension verification.
 *
 * This testbench builds RV32IM instructions programmatically and runs them
 * directly on the ISS, without requiring a firmware binary or RISC-V
 * toolchain. Each test writes its result to a dedicated MMIO register slot
 * at 0x1000_0000 + offset. After execution, the testbench reads back the
 * SV shadow registers and compares against expected values.
 *
 * NOTE: We define VL_DPIDECL_ macros to suppress the auto-generated
 * DPI wrappers from Vtb_top_mmio_regs__Dpi.cpp, and provide our own
 * C-linkage implementations that access the Verilator model directly.
 */

#define VL_DPIDECL_dpi_mmio_read_
#define VL_DPIDECL_dpi_mmio_write_

#include "Vtb_top_mmio_regs.h"
#include "Vtb_top_mmio_regs___024root.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static Vtb_top_mmio_regs *tb = nullptr;

// -----------------------------------------------------------------------
// DPI export implementations (direct model access)
// -----------------------------------------------------------------------

extern "C" int dpi_mmio_read(int addr) {
    if (addr >= 0x10000000 && addr < 0x10000100) {
        int idx = (addr - 0x10000000) / 4;
        return tb->rootp->tb_top_mmio_regs__DOT__hw_cfg_reg[idx];
    }
    return 0;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    if (addr >= 0x10000000 && addr < 0x10000100) {
        int idx = (addr - 0x10000000) / 4;
        tb->rootp->tb_top_mmio_regs__DOT__hw_cfg_reg[idx] = data;
    }
}

// -----------------------------------------------------------------------
// RISC-V instruction helpers (RV32I + M)
// -----------------------------------------------------------------------

/** addi rd, rs1, imm12 */
static uint32_t make_addi(unsigned rd, unsigned rs1, int32_t imm) {
    return ((uint32_t)(imm & 0xfff) << 20) | (rs1 << 15) | (0 << 12) | (rd << 7) | 0x13u;
}

/** lui rd, imm20 */
static uint32_t make_lui(unsigned rd, uint32_t imm20) {
    return (imm20 << 12) | (rd << 7) | 0x37u;
}

/** sw rs2, offset(rs1) */
static uint32_t make_sw(unsigned rs2, unsigned rs1, int32_t imm) {
    uint32_t imm11_5 = ((uint32_t)imm >> 5) & 0x7fu;
    uint32_t imm4_0  = (uint32_t)imm & 0x1fu;
    return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (2u << 12) | (imm4_0 << 7) | 0x23u;
}

/** ebreak – ends execution */
static uint32_t make_ebreak(void) {
    return 0x00100073u;
}

/**
 * M-extension instructions (funct7 = 0x01, opcode = 0x33)
 * mul rd, rs1, rs2
 */
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

/**
 * Load a 32-bit constant into a register using lui + addi.
 * Handles the case where the lower 12 bits have bit 11 set
 * (which would cause addi to sign-extend negatively).
 */
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
// Test case definitions
// =======================================================================

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

// =======================================================================
// Firmware builder — constructs the test program in RAM
// =======================================================================

/**
 * Register map:
 *   t0 (x5)  = MMIO_BASE (0x1000_0000)
 *   t1 (x6)  = scratch operand 1
 *   t2 (x7)  = scratch operand 2
 *   t3 (x28) = result register (avoid x8-x19 which may be used by load_imm)
 *   t4 (x29) = scratch for load_imm
 *   t5 (x30) = scratch for load_imm
 *   t6 (x31) = scratch for load_imm
 */
#define MMIO_BASE_REG 5
#define OP1_REG 6
#define OP2_REG 7
#define RES_REG 28

static void build_firmware(uint32_t *ram) {
    unsigned a = 0;

    // ---- Prologue: set MMIO base ----
    ram[a++] = make_lui(MMIO_BASE_REG, 0x10000u);   // t0 = 0x1000_0000
    ram[a++] = make_addi(MMIO_BASE_REG, MMIO_BASE_REG, 0);

    // ---- Test 0: MUL 7 * 3 = 21 ----
    ram[a++] = make_addi(OP1_REG, 0, 7);
    ram[a++] = make_addi(OP2_REG, 0, 3);
    ram[a++] = make_mul(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 0);   // reg[0] = 21

    // ---- Test 1: MULH 0x12345678^2 upper = 0x014B66DC ----
    load_imm(ram, &a, OP1_REG, 0x12345678u);
    ram[a++] = make_mulh(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 4);   // reg[1]

    // ---- Test 2: MULHSU -1000 * 2000000 upper = 0xFFFFFFFF ----
    ram[a++] = make_addi(OP1_REG, 0, -1000);          // signed -1000
    load_imm(ram, &a, OP2_REG, 2000000u);             // unsigned 2000000
    ram[a++] = make_mulhsu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 8);   // reg[2]

    // ---- Test 3: MULHU 0xFFFFFFFF^2 upper = 0xFFFFFFFE ----
    ram[a++] = make_addi(OP1_REG, 0, -1);             // 0xFFFFFFFF
    ram[a++] = make_mulhu(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 12);  // reg[3]

    // ---- Test 4: DIV 100 / 3 = 33 ----
    ram[a++] = make_addi(OP1_REG, 0, 100);
    ram[a++] = make_addi(OP2_REG, 0, 3);
    ram[a++] = make_div(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 16);  // reg[4]

    // ---- Test 5: DIVU 200 / 7 = 28 ----
    ram[a++] = make_addi(OP1_REG, 0, 200);
    ram[a++] = make_addi(OP2_REG, 0, 7);
    ram[a++] = make_divu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 20);  // reg[5]

    // ---- Test 6: REM 100 % 3 = 1 ----
    ram[a++] = make_addi(OP1_REG, 0, 100);
    ram[a++] = make_addi(OP2_REG, 0, 3);
    ram[a++] = make_rem(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 24);  // reg[6]

    // ---- Test 7: REMU 200 % 7 = 4 ----
    ram[a++] = make_addi(OP1_REG, 0, 200);
    ram[a++] = make_addi(OP2_REG, 0, 7);
    ram[a++] = make_remu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 28);  // reg[7]

    // ---- Test 8: DIV by zero -> 0xFFFFFFFF ----
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_div(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 32);  // reg[8]

    // ---- Test 9: DIVU by zero -> 0xFFFFFFFF ----
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_divu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 36);  // reg[9]

    // ---- Test 10: REM by zero -> 50 (dividend) ----
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_rem(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 40);  // reg[10]

    // ---- Test 11: REMU by zero -> 50 (dividend) ----
    ram[a++] = make_addi(OP1_REG, 0, 50);
    ram[a++] = make_addi(OP2_REG, 0, 0);
    ram[a++] = make_remu(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 44);  // reg[11]

    // ---- Test 12: INT32_MIN / -1 -> INT32_MIN (overflow) ----
    load_imm(ram, &a, OP1_REG, (uint32_t)INT32_MIN);
    ram[a++] = make_addi(OP2_REG, 0, -1);
    ram[a++] = make_div(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 48);  // reg[12]

    // ---- Test 13: INT32_MIN % -1 -> 0 (overflow) ----
    load_imm(ram, &a, OP1_REG, (uint32_t)INT32_MIN);
    ram[a++] = make_addi(OP2_REG, 0, -1);
    ram[a++] = make_rem(RES_REG, OP1_REG, OP2_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 52);  // reg[13]

    // ---- Test 14: MUL 0x10000001^2 low = 0x20000001 ----
    load_imm(ram, &a, OP1_REG, 0x10000001u);
    ram[a++] = make_mul(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 56);  // reg[14]

    // ---- Test 15: MULH 0x10000001^2 upper = 0x01000000 ----
    // OP1_REG still holds 0x10000001 from test 14
    ram[a++] = make_mulh(RES_REG, OP1_REG, OP1_REG);
    ram[a++] = make_sw(RES_REG, MMIO_BASE_REG, 60);  // reg[15]

    // ---- Epilogue: ebreak ----
    ram[a] = make_ebreak();
}

// =======================================================================
// Helper: read the SV hw_cfg_reg array from the Verilator model
// =======================================================================

static uint32_t read_sv_reg(unsigned idx) {
    if (idx >= 64) return 0;
    return tb->rootp->tb_top_mmio_regs__DOT__hw_cfg_reg[idx];
}

static void print_sv_regs(void) {
    printf("\nSV hw_cfg_reg state (after firmware execution):\n");
    for (unsigned i = 0; i < num_tests; i++) {
        printf("  reg[%2u] = 0x%08x\n", test_cases[i].reg_idx, read_sv_reg(test_cases[i].reg_idx));
    }
}

// =======================================================================
// Main
// =======================================================================
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    tb = new Vtb_top_mmio_regs;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5);
    tfp->open("tb_top_muldiv.vcd");

    printf("=== RV32M (MUL/DIV/REM) Extension Test ===\n");

    // ---- Initialise the ISS ----
    rv_init(NULL, 1 << 20);                // 1 MiB shared RAM
    uint32_t *ram = (uint32_t *)rv_get_ram();

    // ---- Build firmware programmatically ----
    build_firmware(ram);
    printf("Firmware built at address 0x0000_0000\n");
    printf("RAM[0..7]: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
           ram[0], ram[1], ram[2], ram[3], ram[4], ram[5], ram[6], ram[7]);

    // ---- Reset the ISS ----
    rv_reset(0);

    // ---- Initialise the RTL (Verilator model) ----
    tb->rootp->tb_top_mmio_regs__DOT__clk = 0;
    tb->rootp->tb_top_mmio_regs__DOT__rst = 1;

    // Run reset for 2 clock edges
    for (int i = 0; i < 2; ++i) {
        tb->rootp->tb_top_mmio_regs__DOT__clk = !tb->rootp->tb_top_mmio_regs__DOT__clk;
        tb->eval();
    }
    tb->rootp->tb_top_mmio_regs__DOT__rst = 0;

    printf("Starting firmware execution...\n");

    // ---- Execute the firmware in the ISS ----
    // The firmware has ~80 instructions, run up to 200 to be safe
    int executed = rv_step(200);
    printf("rv_step: executed %d instructions\n", executed);

    // ---- Tick the RTL clock a few more edges ----
    for (int i = 0; i < 10; ++i) {
        tb->rootp->tb_top_mmio_regs__DOT__clk = !tb->rootp->tb_top_mmio_regs__DOT__clk;
        tb->eval();
        tfp->dump(i);
    }

    // ---- Print results ----
    print_sv_regs();

    // ---- Verify expected register values ----
    printf("\n--- Results ---\n");
    unsigned pass_count = 0;
    unsigned fail_count = 0;
    for (unsigned i = 0; i < num_tests; i++) {
        uint32_t actual = read_sv_reg(test_cases[i].reg_idx);
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

    // ---- Tear down ----
    tfp->close();
    tb->final();
    delete tb;
    delete tfp;

    return (fail_count > 0) ? 1 : 0;
}
