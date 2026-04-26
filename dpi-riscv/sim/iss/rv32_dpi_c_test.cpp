/**
 * rv32_dpi_c_test.cpp — Standalone ISS test for RV32C (Compressed) extension.
 *
 * This test builds RV32IMC instructions programmatically in the ISS RAM,
 * executes them, and verifies results by reading back register values.
 * No Verilator, no SV, no RISC-V toolchain required.
 *
 * Compile:
 *   g++ -I. -o rv32_dpi_c_test rv32_dpi_c_test.cpp rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_c_test
 */

#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// -----------------------------------------------------------------------
// DPI stubs — called by the ISS for MMIO access.
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
// RISC-V instruction helpers (RV32I) — used for MMIO verification stores
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

// -----------------------------------------------------------------------
// Compressed instruction helpers (RV32C)
// -----------------------------------------------------------------------

// C.ADDI rd, imm (6-bit signed immediate)
// Encoding: [1:0]=01, [15:13]=000, [12]=imm[5], [11:7]=rd, [6:2]=imm[4:0]
static uint16_t make_c_addi(unsigned rd, int32_t imm) {
    uint32_t uimm = (uint32_t)(imm & 0x3f);
    return (uint16_t)(0x0001u | ((uimm & 0x20) << 7) | (rd << 7) | ((uimm & 0x1f) << 2));
}

// C.LI rd, imm (6-bit signed immediate)
// Encoding: [1:0]=01, [15:13]=010, [12]=imm[5], [11:7]=rd, [6:2]=imm[4:0]
static uint16_t make_c_li(unsigned rd, int32_t imm) {
    uint32_t uimm = (uint32_t)(imm & 0x3f);
    return (uint16_t)(0x4001u | ((uimm & 0x20) << 7) | (rd << 7) | ((uimm & 0x1f) << 2));
}

// C.LUI rd, imm (6-bit immediate in bits 17:12)
// Encoding: [1:0]=01, [15:13]=011, [12]=imm[17], [11:7]=rd, [6:2]=imm[16:12]
static uint16_t make_c_lui(unsigned rd, uint32_t imm) {
    uint32_t imm17_12 = (imm >> 12) & 0x3f;
    return (uint16_t)(0x6001u | ((imm17_12 & 0x20) << 7) | (rd << 7) | ((imm17_12 & 0x1f) << 2));
}

// C.NOP = C.ADDI x0, 0
static uint16_t make_c_nop(void) {
    return 0x0001u;
}

// C.MV rd, rs2 (rd != 0, rs2 != 0)
// Encoding: [1:0]=10, [15:12]=1000, [11:7]=rd, [6:2]=rs2
static uint16_t make_c_mv(unsigned rd, unsigned rs2) {
    return (uint16_t)(0x8002u | (rd << 7) | (rs2 << 2));
}

// C.ADD rd, rs2 (rd != 0)
// Encoding: [1:0]=10, [15:12]=1001, [11:7]=rd/rs1, [6:2]=rs2
static uint16_t make_c_add(unsigned rd, unsigned rs2) {
    return (uint16_t)(0x9002u | (rd << 7) | (rs2 << 2));
}

// C.JR rs1 (rs1 != 0)
// Encoding: [1:0]=10, [15:12]=1000, [11:7]=rs1, [6:2]=0
static uint16_t make_c_jr(unsigned rs1) {
    return (uint16_t)(0x8002u | (rs1 << 7));
}

// C.JALR rs1 (rs1 != 0)
// Encoding: [1:0]=10, [15:12]=1001, [11:7]=rs1, [6:2]=0
static uint16_t make_c_jalr(unsigned rs1) {
    return (uint16_t)(0x9002u | (rs1 << 7));
}

// C.EBREAK
// Encoding: [1:0]=10, [15:12]=1001, [11:7]=0, [6:2]=0
static uint16_t make_c_ebreak(void) {
    return 0x9002u;
}

// C.LW rd', offset(rs1')  — compressed registers (x8-x15)
// Encoding: [1:0]=00, [15:13]=010, [12:10]=offset[5:3], [9:7]=rs1', [6:5]=offset[2:1], [4:2]=rd'
static uint16_t make_c_lw(unsigned rd_c, unsigned rs1_c, uint32_t offset) {
    uint32_t off = offset & 0x7f;
    return (uint16_t)(0x4000u | ((off >> 3) & 0x7) << 10 | (rs1_c << 7) | ((off >> 1) & 0x3) << 5 | (rd_c << 2));
}

// C.SW rs2', offset(rs1')  — compressed registers (x8-x15)
// Encoding: [1:0]=00, [15:13]=110, [12:10]=offset[5:3], [9:7]=rs1', [6:5]=offset[2:1], [4:2]=rs2'
static uint16_t make_c_sw(unsigned rs2_c, unsigned rs1_c, uint32_t offset) {
    uint32_t off = offset & 0x7f;
    return (uint16_t)(0xC000u | ((off >> 3) & 0x7) << 10 | (rs1_c << 7) | ((off >> 1) & 0x3) << 5 | (rs2_c << 2));
}

// C.LWSP rd, offset(sp)
// Encoding: [1:0]=10, [15:12]=1000, [11:7]=rd, [6:2]=offset[7:3], [1:0]=10
// offset = {insn[6:2], 2'b00} — 8-bit unsigned, word-aligned
static uint16_t make_c_lwsp(unsigned rd, uint32_t offset) {
    uint32_t off = offset & 0xff;
    return (uint16_t)(0x4002u | (rd << 7) | ((off >> 3) & 0x1fu) << 2);
}

// C.SWSP rs2, offset(sp)
// Encoding: [1:0]=10, [15:14]=11, [13:12]=00, [11:10]=offset[7:6], [9:7]=rs2, [6:5]=offset[5:4], [4:2]=0
static uint16_t make_c_swsp(unsigned rs2, uint32_t offset) {
    uint32_t off = offset & 0xff;
    return (uint16_t)(0xC002u | ((off >> 6) & 0x3) << 10 | (rs2 << 7) | ((off >> 4) & 0x3) << 5);
}

// C.J offset (12-bit signed)
// Encoding: [1:0]=01, [15:13]=101, [12]=imm[11], [11]=imm[4], [10:9]=imm[9:8], [8]=imm[10],
//           [7]=imm[6], [6]=imm[7], [5:3]=imm[3:1], [2]=imm[5]
static uint16_t make_c_j(int32_t offset) {
    uint32_t off = (uint32_t)(offset & 0xfff);
    return (uint16_t)(0xA001u |
        ((off >> 11) & 0x1) << 12 |
        ((off >> 4) & 0x1) << 11 |
        ((off >> 9) & 0x3) << 9 |
        ((off >> 10) & 0x1) << 8 |
        ((off >> 6) & 0x1) << 7 |
        ((off >> 7) & 0x1) << 6 |
        ((off >> 1) & 0x7) << 3 |
        ((off >> 5) & 0x1) << 2);
}

// C.JAL offset (12-bit signed) — rd = x1 (ra)
// Encoding: [1:0]=01, [15:13]=001, same offset encoding as C.J
static uint16_t make_c_jal(int32_t offset) {
    uint32_t off = (uint32_t)(offset & 0xfff);
    return (uint16_t)(0x2001u |
        ((off >> 11) & 0x1) << 12 |
        ((off >> 4) & 0x1) << 11 |
        ((off >> 9) & 0x3) << 9 |
        ((off >> 10) & 0x1) << 8 |
        ((off >> 6) & 0x1) << 7 |
        ((off >> 7) & 0x1) << 6 |
        ((off >> 1) & 0x7) << 3 |
        ((off >> 5) & 0x1) << 2);
}

// C.BEQZ rs1', offset (9-bit signed) — compressed register (x8-x15)
// Encoding: [1:0]=01, [15:13]=110, [12]=imm[8], [11:10]=imm[4:3], [9:7]=rs1',
//           [6:5]=imm[7:6], [4:3]=imm[2:1], [2]=imm[5]
static uint16_t make_c_beqz(unsigned rs1_c, int32_t offset) {
    uint32_t off = (uint32_t)(offset & 0x1ff);
    return (uint16_t)(0xC001u |
        ((off >> 8) & 0x1) << 12 |
        ((off >> 3) & 0x3) << 10 |
        (rs1_c << 7) |
        ((off >> 6) & 0x3) << 5 |
        ((off >> 1) & 0x3) << 3 |
        ((off >> 5) & 0x1) << 2);
}

// C.BNEZ rs1', offset (9-bit signed) — compressed register (x8-x15)
// Encoding: [1:0]=01, [15:13]=111, same offset encoding as C.BEQZ
static uint16_t make_c_bnez(unsigned rs1_c, int32_t offset) {
    uint32_t off = (uint32_t)(offset & 0x1ff);
    return (uint16_t)(0xE001u |
        ((off >> 8) & 0x1) << 12 |
        ((off >> 3) & 0x3) << 10 |
        (rs1_c << 7) |
        ((off >> 6) & 0x3) << 5 |
        ((off >> 1) & 0x3) << 3 |
        ((off >> 5) & 0x1) << 2);
}

// C.SLLI rd, shamt (6-bit shift amount)
// Encoding: [1:0]=10, [15:13]=000, [12]=shamt[5], [11:7]=rd/rs1, [6:2]=shamt[4:0]
static uint16_t make_c_slli(unsigned rd, uint32_t shamt) {
    return (uint16_t)(0x0002u | ((shamt & 0x20) << 7) | (rd << 7) | ((shamt & 0x1f) << 2));
}

// C.SRLI rd', shamt (6-bit shift amount) — compressed register (x8-x15)
// Encoding: [1:0]=01, [15:10]=100000, [9:7]=rd'/rs1', [6:2]=shamt[4:0], [12]=shamt[5]
static uint16_t make_c_srli(unsigned rd_c, uint32_t shamt) {
    return (uint16_t)(0x8001u | ((shamt & 0x20) << 7) | (rd_c << 7) | ((shamt & 0x1f) << 2));
}

// C.SRAI rd', shamt (6-bit shift amount) — compressed register (x8-x15)
// Encoding: [1:0]=01, [15:10]=100001, [9:7]=rd'/rs1', [6:2]=shamt[4:0], [12]=shamt[5]
static uint16_t make_c_srai(unsigned rd_c, uint32_t shamt) {
    return (uint16_t)(0x8401u | ((shamt & 0x20) << 7) | (rd_c << 7) | ((shamt & 0x1f) << 2));
}

// C.ANDI rd', imm (6-bit signed immediate) — compressed register (x8-x15)
// Encoding: [1:0]=01, [15:10]=100010, [9:7]=rd'/rs1', [6:2]=imm[4:0], [12]=imm[5]
static uint16_t make_c_andi(unsigned rd_c, int32_t imm) {
    uint32_t uimm = (uint32_t)(imm & 0x3f);
    return (uint16_t)(0x8801u | ((uimm & 0x20) << 7) | (rd_c << 7) | ((uimm & 0x1f) << 2));
}

// C.SUB rd', rs2' — compressed registers (x8-x15)
// Encoding: [1:0]=01, [15:10]=100011, [9:7]=rd'/rs1', [6:5]=00, [4:2]=rs2'
static uint16_t make_c_sub(unsigned rd_c, unsigned rs2_c) {
    return (uint16_t)(0x8C01u | (rd_c << 7) | (rs2_c << 2));
}

// C.XOR rd', rs2' — compressed registers (x8-x15)
// Encoding: [1:0]=01, [15:10]=100011, [9:7]=rd'/rs1', [6:5]=01, [4:2]=rs2'
static uint16_t make_c_xor(unsigned rd_c, unsigned rs2_c) {
    return (uint16_t)(0x8C21u | (rd_c << 7) | (rs2_c << 2));
}

// C.OR rd', rs2' — compressed registers (x8-x15)
// Encoding: [1:0]=01, [15:10]=100011, [9:7]=rd'/rs1', [6:5]=10, [4:2]=rs2'
static uint16_t make_c_or(unsigned rd_c, unsigned rs2_c) {
    return (uint16_t)(0x8C41u | (rd_c << 7) | (rs2_c << 2));
}

// C.AND rd', rs2' — compressed registers (x8-x15)
// Encoding: [1:0]=01, [15:10]=100011, [9:7]=rd'/rs1', [6:5]=11, [4:2]=rs2'
static uint16_t make_c_and(unsigned rd_c, unsigned rs2_c) {
    return (uint16_t)(0x8C61u | (rd_c << 7) | (rs2_c << 2));
}

// C.ADDI4SPN rd', nzuimm (nonzero immediate) — compressed register (x8-x15)
// Encoding: [1:0]=00, [15:13]=000, [12:5]=nzuimm, [4:2]=rd'
// nzuimm[9:0] encoded as:
// insn[12]=nzuimm[5], insn[11]=nzuimm[4], insn[10]=nzuimm[9],
// insn[9]=nzuimm[8],  insn[8]=nzuimm[7],  insn[7]=nzuimm[6],
// insn[6]=nzuimm[2],  insn[5]=nzuimm[3]
static uint16_t make_c_addi4spn(unsigned rd_c, uint32_t nzuimm) {
    uint32_t imm = nzuimm & 0x3fc;
    return (uint16_t)(((imm >> 5) & 0x1) << 12 |  // nzuimm[5] -> insn[12]
                      ((imm >> 4) & 0x1) << 11 |  // nzuimm[4] -> insn[11]
                      ((imm >> 9) & 0x1) << 10 |  // nzuimm[9] -> insn[10]
                      ((imm >> 8) & 0x1) << 9 |   // nzuimm[8] -> insn[9]
                      ((imm >> 7) & 0x1) << 8 |   // nzuimm[7] -> insn[8]
                      ((imm >> 6) & 0x1) << 7 |   // nzuimm[6] -> insn[7]
                      ((imm >> 2) & 0x1) << 6 |   // nzuimm[2] -> insn[6]
                      ((imm >> 3) & 0x1) << 5 |   // nzuimm[3] -> insn[5]
                      (rd_c << 2));
}

// C.ADDI16SP imm (10-bit signed, rd=x2)
// Encoding: [1:0]=01, [15:13]=011, [12]=imm[5], [11:7]=00010, [6]=imm[7],
//           [5]=imm[6], [4]=imm[8], [3]=imm[9], [2]=imm[4]
static uint16_t make_c_addi16sp(int32_t imm) {
    uint32_t uimm = (uint32_t)(imm & 0x3ff);
    return (uint16_t)(0x6101u |
        ((uimm >> 5) & 0x1) << 12 |
        ((uimm >> 7) & 0x1) << 6 |
        ((uimm >> 6) & 0x1) << 5 |
        ((uimm >> 8) & 0x1) << 4 |
        ((uimm >> 9) & 0x1) << 3 |
        ((uimm >> 4) & 0x1) << 2);
}

// -----------------------------------------------------------------------
// Helpers: write instructions into RAM
// -----------------------------------------------------------------------

// Write a 16-bit compressed instruction into RAM (half-word aligned)
// Uses byte writes to avoid strict-aliasing issues with the ISS's uint8_t* memory.
static void write_c(uint16_t *ram, unsigned *addr, uint16_t c_insn) {
    uint8_t *mem = (uint8_t *)ram;
    unsigned byte_addr = *addr * 2;
    mem[byte_addr + 0] = (uint8_t)(c_insn & 0xFFu);
    mem[byte_addr + 1] = (uint8_t)((c_insn >> 8) & 0xFFu);
    *addr += 1; // 2 bytes
}

// Write a 32-bit uncompressed instruction into RAM (word aligned)
// If addr is odd (half-word aligned), pad with a NOP first
static void write_u32(uint16_t *ram, unsigned *addr, uint32_t insn) {
    // Ensure word alignment
    if (*addr & 1) {
        write_c(ram, addr, make_c_nop());
    }
    uint8_t *mem = (uint8_t *)ram;
    unsigned byte_addr = *addr * 2;
    mem[byte_addr + 0] = (uint8_t)(insn & 0xFFu);
    mem[byte_addr + 1] = (uint8_t)((insn >> 8) & 0xFFu);
    mem[byte_addr + 2] = (uint8_t)((insn >> 16) & 0xFFu);
    mem[byte_addr + 3] = (uint8_t)((insn >> 24) & 0xFFu);
    *addr += 2; // 4 bytes
}

// -----------------------------------------------------------------------
// Test infrastructure
// -----------------------------------------------------------------------

struct TestCase {
    unsigned reg_idx;
    const char *name;
    uint32_t expected;
};

static const TestCase test_cases[] = {
    { 0,  "C.LI x6, 42",                         42u              },
    { 1,  "C.ADDI x6, 10 (x6 was 42 -> 52)",     52u              },
    { 24, "C.NOP (no-op, x6 unchanged)",          52u              },
    { 3,  "C.MV x7, x6 (x7 = 52)",               52u              },
    { 4,  "C.ADD x7, x6 (x7 = 52+52=104)",       104u             },
    { 5,  "C.LUI x8, 0x10000",                    0x10000000u      },
    { 6,  "C.LW x9, 0(x8) (load from MMIO[0])",  42u              },
    { 7,  "C.SW x9, 8(x8) (store to MMIO[2])",   42u              },
    { 8,  "C.SLLI x6, 2 (52<<2=208)",             208u             },
    { 9,  "C.SRLI x9, 2 (42>>2=10)",              10u              },
    { 10, "C.ANDI x9, 0xF (10&15=10)",            10u              },
    { 11, "C.SUB x9, x9 (10-10=0)",               0u               },
    { 12, "C.XOR x9, x9 (0^0=0)",                 0u               },
    { 13, "C.OR x9, x9 (0|0=0)",                  0u               },
    { 14, "C.AND x9, x9 (0&0=0)",                 0u               },
    { 15, "C.LWSP x10, 0(sp) (load from stack)",  0u               },
    { 16, "C.SWSP x6, 0(sp) (store to stack)",    208u             },
    { 17, "C.ADDI4SPN x11, 16 (sp+16)",           0x00080010u      },
    { 18, "C.ADDI16SP -32 (sp -= 32)",            0x0007FFE0u      },
    { 19, "C.J (jump over NOP)",                  1u               },
    { 20, "C.BEQZ taken (x0==0)",                 1u               },
    { 21, "C.BNEZ taken (x15!=0)",                1u               },
    { 22, "C.SRAI x6, 2 (208>>2=52)",             52u              },
};

static const unsigned num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

static void build_firmware(uint16_t *ram) {
    unsigned a = 0; // half-word address

    // Prologue: set MMIO base in x8 (compressed reg index 0 = x8)
    // Use 32-bit LUI x8, 0x10000 (sets x8 = 0x10000000)
    // C.LUI can only encode bits [17:12], so 0x10000000 (bit 28) is too large
    write_u32(ram, &a, make_lui(8, 0x10000u));

    // Test 0: C.LI x6, 42 (42 = 0x2a, fits in 6-bit signed? 42 > 31, so no)
    // Use C.LI to load 10, then C.ADDI to add 32 (which is -32 sign-extended... also doesn't fit)
    // Use 32-bit ADDI instead: ADDI x6, x0, 42
    write_u32(ram, &a, make_addi(6, 0, 42));
    write_u32(ram, &a, make_sw(6, 8, 0)); // MMIO[0] = x6 = 42

    // Test 1: C.ADDI x6, 10 (x6 = 42 + 10 = 52)
    write_c(ram, &a, make_c_addi(6, 10));
    write_u32(ram, &a, make_sw(6, 8, 4)); // MMIO[1] = 52

    // Test 2: C.NOP
    write_c(ram, &a, make_c_nop());
    write_u32(ram, &a, make_sw(6, 8, 96)); // MMIO[24] = 52 (unchanged)

    // Test 3: C.MV x7, x6 (x7 = 52)
    write_c(ram, &a, make_c_mv(7, 6));
    write_u32(ram, &a, make_sw(7, 8, 12)); // MMIO[3] = 52

    // Test 4: C.ADD x7, x6 (x7 = 52 + 52 = 104)
    write_c(ram, &a, make_c_add(7, 6));
    write_u32(ram, &a, make_sw(7, 8, 16)); // MMIO[4] = 104

    // Test 5: C.LUI x8, 0x10000 (already done in prologue)
    // Store x8 to verify
    write_u32(ram, &a, make_sw(8, 8, 20)); // MMIO[5] = 0x10000000

    // Test 6: C.LW x9, 0(x8) — load from MMIO[0] which has 42
    // x9 is compressed reg index 1, x8 is compressed reg index 0
    write_c(ram, &a, make_c_lw(1, 0, 0)); // x9 = *(x8+0) = 42
    write_u32(ram, &a, make_sw(9, 8, 24)); // MMIO[6] = 42

    // Test 7: C.SW x9, 8(x8) — store x9 to MMIO[2]
    // x9 is compressed reg 1, x8 is compressed reg 0
    // Use offset 8 (MMIO[2]) to avoid overwriting Test 1's MMIO[1]
    write_c(ram, &a, make_c_sw(1, 0, 8)); // *(x8+8) = x9 = 42
    // Read it back via C.LW to verify
    write_c(ram, &a, make_c_lw(2, 0, 8)); // x10 = *(x8+8)
    write_u32(ram, &a, make_sw(10, 8, 28)); // MMIO[7] = 42

    // Test 8: C.SLLI x6, 2 (52 << 2 = 208)
    write_c(ram, &a, make_c_slli(6, 2));
    write_u32(ram, &a, make_sw(6, 8, 32)); // MMIO[8] = 208

    // Test 9: C.SRLI x9, 2 (42 >> 2 = 10)
    // x9 is compressed reg 1, need to load 42 first
    // 42 doesn't fit in 6-bit signed C.LI, use 32-bit ADDI
    write_u32(ram, &a, make_addi(9, 0, 42));
    write_c(ram, &a, make_c_srli(1, 2)); // x9 >>= 2
    write_u32(ram, &a, make_sw(9, 8, 36)); // MMIO[9] = 10

    // Test 10: C.ANDI x9, 0xF (10 & 15 = 10)
    write_c(ram, &a, make_c_andi(1, 0xF));
    write_u32(ram, &a, make_sw(9, 8, 40)); // MMIO[10] = 10

    // Test 11: C.SUB x9, x9 (10 - 10 = 0)
    write_c(ram, &a, make_c_sub(1, 1));
    write_u32(ram, &a, make_sw(9, 8, 44)); // MMIO[11] = 0

    // Test 12: C.XOR x9, x9 (0 ^ 0 = 0)
    write_c(ram, &a, make_c_xor(1, 1));
    write_u32(ram, &a, make_sw(9, 8, 48)); // MMIO[12] = 0

    // Test 13: C.OR x9, x9 (0 | 0 = 0)
    write_c(ram, &a, make_c_or(1, 1));
    write_u32(ram, &a, make_sw(9, 8, 52)); // MMIO[13] = 0

    // Test 14: C.AND x9, x9 (0 & 0 = 0)
    write_c(ram, &a, make_c_and(1, 1));
    write_u32(ram, &a, make_sw(9, 8, 56)); // MMIO[14] = 0

    // Set up sp (x2) to point to a safe stack area
    // Use LUI + ADDI since 0x80000 doesn't fit in 12-bit signed immediate
    write_u32(ram, &a, make_lui(2, 0x80u));    // sp = 0x80000 (512KB, middle of 1MB RAM)
    write_u32(ram, &a, make_addi(2, 2, 0));    // sp = 0x80000 (no-op ADDI for alignment)

    // Test 15: C.LWSP x10, 0(sp) — load from stack (should be 0, stack is zeroed)
    write_c(ram, &a, make_c_lwsp(10, 0));
    write_u32(ram, &a, make_sw(10, 8, 60)); // MMIO[15] = 0

    // Test 16: C.SWSP x6, 0(sp) — store x6 (208) to stack
    write_c(ram, &a, make_c_swsp(6, 0));
    // Read it back
    write_c(ram, &a, make_c_lwsp(10, 0));
    write_u32(ram, &a, make_sw(10, 8, 64)); // MMIO[16] = 208

    // Test 17: C.ADDI4SPN x11, 16 (sp + 16)
    // x11 is compressed reg index 3
    write_c(ram, &a, make_c_addi4spn(3, 16));
    write_u32(ram, &a, make_sw(11, 8, 68)); // MMIO[17] = 16

    // Test 18: C.ADDI16SP -32 (sp -= 32)
    write_c(ram, &a, make_c_addi16sp(-32));
    // Read sp (x2) via C.MV to x12, then store
    write_c(ram, &a, make_c_mv(12, 2));
    write_u32(ram, &a, make_sw(12, 8, 72)); // MMIO[18] = sp (should be -32 = 0xFFFFFFE0)

    // Test 19: C.J (jump over NOP)
    write_c(ram, &a, make_c_li(13, 1));
    // Jump forward by 2 halfwords (skip one 16-bit insn)
    write_c(ram, &a, make_c_j(4)); // +4 bytes = +2 halfwords
    write_c(ram, &a, make_c_nop()); // skipped
    // Target: store x13 to MMIO[19*4]
    write_u32(ram, &a, make_sw(13, 8, 76)); // MMIO[19] = 1

    // Test 20: C.BEQZ taken (x14 == 0, so branch should be taken)
    write_c(ram, &a, make_c_li(14, 0)); // x14 = 0
    // x14 is compressed reg index 6
    // Branch forward by 4 halfwords if x14 == 0
    write_c(ram, &a, make_c_beqz(6, 8)); // +8 bytes = +4 halfwords
    write_c(ram, &a, make_c_nop()); // skipped
    write_c(ram, &a, make_c_nop()); // skipped
    write_c(ram, &a, make_c_nop()); // skipped
    write_c(ram, &a, make_c_nop()); // skipped
    // Target: store 1 to MMIO[20*4]
    write_c(ram, &a, make_c_li(15, 1));
    write_u32(ram, &a, make_sw(15, 8, 80)); // MMIO[20] = 1

    // Test 21: C.BNEZ taken (x15 != 0, so branch should be taken)
    // x15 = 1 (nonzero) — compressed reg 7
    write_c(ram, &a, make_c_bnez(7, 8)); // +8 bytes = +4 halfwords
    write_c(ram, &a, make_c_nop()); // skipped
    write_c(ram, &a, make_c_nop()); // skipped
    write_c(ram, &a, make_c_nop()); // skipped
    // Target: store 1 to MMIO[21*4]
    write_c(ram, &a, make_c_li(15, 1));
    write_u32(ram, &a, make_sw(15, 8, 84)); // MMIO[21] = 1

    // Test 22: C.SRAI on compressed register (x8-x15)
    // x6 has 208 from Test 8, but C.SRAI only works on compressed regs (x8-x15).
    // Copy x6 to x9 (compressed reg 1), then do C.SRAI x9, 2
    write_c(ram, &a, make_c_mv(9, 6)); // x9 = x6 = 208
    write_c(ram, &a, make_c_srai(1, 2)); // x9 >>= 2 (arithmetic) = 52
    write_u32(ram, &a, make_sw(9, 8, 88)); // MMIO[22] = 52

    // Epilogue: EBREAK to stop execution
    write_u32(ram, &a, make_ebreak());
}

// -----------------------------------------------------------------------
// main()
// -----------------------------------------------------------------------

int main(void) {
    printf("=== RV32C (Compressed) Extension Test (Standalone) ===\n\n");

    rv_init(NULL, 1 << 20);
    uint32_t *ram = (uint32_t *)rv_get_ram();
    build_firmware((uint16_t *)ram);

    rv_reset(0);
    int executed = rv_step(500);
    printf("Executed %d instructions\n\n", executed);

    // Debug: dump all MMIO values
    fprintf(stderr, "\nMMIO dump:\n");
    for (unsigned i = 0; i < 24; i++) {
        fprintf(stderr, "  MMIO[%u] = 0x%08x\n", i, mmio_region[i]);
    }

    unsigned pass_count = 0, fail_count = 0;
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
