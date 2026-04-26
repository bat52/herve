/**
 * Simulation testcase: Software configuring hardware registers via MMIO.
 *
 * This testbench demonstrates a RISC-V program that configures several
 * hardware registers (in a peripheral MMIO region) by writing to them
 * sequentially, then reads them back to verify correctness.
 *
 * Scenario:
 *   A RISC-V firmware program executes in the DPI ISS and performs
 *   MMIO writes to a set of peripheral registers at addresses
 *   0x1000_0000 - 0x1000_000C (4 x 32-bit "GPIO" registers).
 *   The SystemVerilog RTL captures these writes and stores them in
 *   its own register array. After the program runs, the testbench
 *   checks that the RTL's shadow registers contain the expected values.
 *
 * This simulates a typical boot-time or driver initialization flow.
 *
 * NOTE: We define VL_DPIDECL_ macros to suppress the auto-generated
 * DPI wrappers from Vtb_top_mmio_regs__Dpi.cpp, and provide our own
 * C-linkage implementations that access the Verilator model directly.
 * This is necessary because the ISS calls DPI exports from pure C code
 * outside Verilator's eval context.
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

static Vtb_top_mmio_regs *tb = nullptr;

// -----------------------------------------------------------------------
// DPI export implementations (direct model access)
//
// Instead of routing through Verilator's DPI export dispatch (which
// requires proper scope context), we directly read/write the SV signals
// through the Verilator model's hierarchy.
// -----------------------------------------------------------------------

extern "C" int dpi_mmio_read(int addr) {
    if (addr >= 0x10000000 && addr < 0x10000010) {
        int idx = (addr - 0x10000000) / 4;
        return tb->rootp->tb_top_mmio_regs__DOT__hw_cfg_reg[idx];
    }
    return 0;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    if (addr >= 0x10000000 && addr < 0x10000010) {
        int idx = (addr - 0x10000000) / 4;
        tb->rootp->tb_top_mmio_regs__DOT__hw_cfg_reg[idx] = data;
    }
}

// -----------------------------------------------------------------------
// RISC-V instruction helpers (RV32I)
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

/** lw rd, offset(rs1) */
static uint32_t make_lw(unsigned rd, unsigned rs1, int32_t imm) {
    return ((uint32_t)(imm & 0xfff) << 20) | (rs1 << 15) | (2u << 12) | (rd << 7) | 0x03u;
}

/** ebreak – ends execution */
static uint32_t make_ebreak(void) {
    return 0x00100073u;
}

/**
 * Load a 32-bit constant into a register using lui + addi.
 * Handles the case where the lower 12 bits have bit 11 set
 * (which would cause addi to sign-extend negatively).
 */
static void load_imm(uint32_t *ram, unsigned *addr, unsigned rd, uint32_t value) {
    uint32_t upper = value >> 12;
    uint32_t lower = value & 0xfff;

    // If bit 11 of the lower part is set, addi will sign-extend it.
    // Compensate by adding 1 to the upper part and using the negative
    // complement for the lower part.
    if (lower & 0x800) {
        upper += 1;
        lower = (int32_t)(int16_t)(lower | 0xfffff000); // sign-extend to 32-bit
        ram[(*addr)++] = make_lui(rd, upper);
        ram[(*addr)++] = make_addi(rd, rd, (int32_t)lower);
    } else {
        ram[(*addr)++] = make_lui(rd, upper);
        ram[(*addr)++] = make_addi(rd, rd, (int32_t)lower);
    }
}

// =======================================================================
// Test program – firmware binary (hand-assembled RV32I)
// =======================================================================

/**
 * Firmware program layout (loaded into the ISS RAM at address 0):
 *
 *   Register map:
 *     x5  (t0) = MMIO_BASE address (0x1000_0000)
 *     x6  (t1) = scratch for config values
 *     x7  (t2) = scratch for config values
 *
 *   Program:
 *     0x0000:  lui  t0, 0x10000         # t0 = 0x1000_0000 (MMIO base)
 *     0x0004:  addi t0, t0, 0x000       # t0 unchanged
 *     0x0008:  load_imm t1, 0xAAAA_AAAB
 *     0x000C:  sw   t1, 0x000(t0)       # hw_cfg_reg[0] = 0xAAAA_AAAB
 *     0x0010:  load_imm t1, 0x5A5
 *     0x0014:  sw   t1, 0x004(t0)       # hw_cfg_reg[1] = 0x5A5
 *     0x0018:  load_imm t1, 0x5555_5554
 *     0x001C:  sw   t1, 0x008(t0)       # hw_cfg_reg[2] = 0x5555_5554
 *     0x0020:  load_imm t1, 0x7FF
 *     0x0024:  sw   t1, 0x00C(t0)       # hw_cfg_reg[3] = 0x7FF
 *     0x0028:  lw   t2, 0x000(t0)       # t2 = readback reg[0]
 *     0x002C:  lw   t2, 0x004(t0)       # t2 = readback reg[1]
 *     0x0030:  lw   t2, 0x008(t0)       # t2 = readback reg[2]
 *     0x0034:  lw   t2, 0x00C(t0)       # t2 = readback reg[3]
 *     0x0038:  ebreak                    # stop simulation
 */

static void load_firmware(uint32_t *ram) {
    unsigned addr = 0;

    // lui t0, 0x10000      -> t0 = 0x1000_0000 (MMIO base)
    ram[addr++] = make_lui(5, 0x10000u);
    ram[addr++] = make_addi(5, 5, 0);

    // t1 = 0xAAAA_AAAB
    load_imm(ram, &addr, 6, 0xAAAAAAABu);
    ram[addr++] = make_sw(6, 5, 0x000);

    // t1 = 0x5A5
    load_imm(ram, &addr, 6, 0x5A5u);
    ram[addr++] = make_sw(6, 5, 0x004);

    // t1 = 0x5555_5554
    load_imm(ram, &addr, 6, 0x55555554u);
    ram[addr++] = make_sw(6, 5, 0x008);

    // t1 = 0x7FF (max positive 12-bit signed immediate)
    load_imm(ram, &addr, 6, 0x7FFu);
    ram[addr++] = make_sw(6, 5, 0x00C);

    // Readback
    ram[addr++] = make_lw(7, 5, 0x000);
    ram[addr++] = make_lw(7, 5, 0x004);
    ram[addr++] = make_lw(7, 5, 0x008);
    ram[addr++] = make_lw(7, 5, 0x00C);

    // ebreak
    ram[addr] = make_ebreak();
}

// =======================================================================
// Helper: read the SV hw_cfg_reg array from the Verilator model
// =======================================================================
static uint32_t read_sv_reg(unsigned idx) {
    if (idx >= 4) return 0;
    return tb->rootp->tb_top_mmio_regs__DOT__hw_cfg_reg[idx];
}

static void print_sv_regs(void) {
    printf("\nSV hw_cfg_reg state (after SW execution):\n");
    for (unsigned i = 0; i < 4; i++) {
        printf("  hw_cfg_reg[%u] = 0x%08x\n", i, read_sv_reg(i));
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
    tfp->open("tb_top_mmio_regs.vcd");

    // ---- Initialise the ISS ----
    rv_init(NULL, 1 << 20);                // 1 MiB shared RAM
    uint32_t *ram = (uint32_t *)rv_get_ram();

    // ---- Load firmware ----
    load_firmware(ram);
    printf("Firmware loaded at address 0x0000_0000\n");

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
    int executed = rv_step(32);
    printf("\nrv_step: executed %d instructions\n", executed);

    // ---- Tick the RTL clock a few more edges ----
    for (int i = 0; i < 10; ++i) {
        tb->rootp->tb_top_mmio_regs__DOT__clk = !tb->rootp->tb_top_mmio_regs__DOT__clk;
        tb->eval();
        tfp->dump(i);
    }

    // ---- Print results ----
    print_sv_regs();

    // ---- Verify expected register values ----
    static const uint32_t expected[4] = {
        0xAAAAAAABu,
        0x000005A5u,
        0x55555554u,
        0x000007FFu
    };

    unsigned pass_count = 0;
    unsigned fail_count = 0;
    for (unsigned i = 0; i < 4; i++) {
        uint32_t actual = read_sv_reg(i);
        if (actual == expected[i]) {
            printf("  [PASS] reg[%u] = 0x%08x (expected 0x%08x)\n",
                   i, actual, expected[i]);
            pass_count++;
        } else {
            printf("  [FAIL] reg[%u] = 0x%08x (expected 0x%08x)\n",
                   i, actual, expected[i]);
            fail_count++;
        }
    }

    printf("\n===========================\n");
    printf("  Passed: %u / 4\n", pass_count);
    printf("  Failed: %u / 4\n", fail_count);
    printf("===========================\n");

    // ---- Tear down ----
    tfp->close();
    tb->final();
    delete tb;
    delete tfp;

    return (fail_count > 0) ? 1 : 0;
}
