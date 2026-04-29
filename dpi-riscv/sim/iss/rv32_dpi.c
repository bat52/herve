#include "rv32_dpi.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Dependency Policy:
 * This file must compile with only standard C headers available
 * in Verilator's DPI compilation environment.
 * Permitted headers: <stdint.h>, <stdbool.h>, <stddef.h>,
 *                    <stdlib.h>, <string.h>, <stdio.h>
 * No libc features beyond malloc/free/memset/memcpy/fopen/fread.
 * No external libraries, no POSIX APIs.
 */

#define MMIO_BASE 0x10000000u
#define MMIO_SIZE 0x00100000u

static uint8_t *memory = NULL;
static uint32_t memory_size = 0;
static uint32_t regs[32];
static uint32_t pc = 0;
static uint32_t irq_mask = 0;
static bool initialized = false;

/* CSR storage */
static uint32_t csr_mstatus = 0;
static uint32_t csr_mtvec = 0;
static uint32_t csr_mepc = 0;
static uint32_t csr_mcause = 0;

#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t dpi_mmio_read(uint32_t addr);
extern void dpi_mmio_write(uint32_t addr, uint32_t value);
#ifdef __cplusplus
}
#endif

static inline bool is_mmio_address(uint32_t addr) {
    return addr >= MMIO_BASE && addr < MMIO_BASE + MMIO_SIZE;
}

static inline uint32_t sign_extend(uint32_t value, unsigned bits) {
    uint32_t shift = 32 - bits;
    return (uint32_t)((int32_t)(value << shift) >> shift);
}

static bool valid_memory_access(uint32_t addr, uint32_t width) {
    return addr + width <= memory_size;
}

static uint32_t read_u32(uint32_t addr) {
    if (is_mmio_address(addr)) {
        return dpi_mmio_read(addr);
    }

    if (!valid_memory_access(addr, 4)) {
        return 0;
    }

    uint32_t value;
    memcpy(&value, &memory[addr], 4);
    return value;
}

static uint32_t read_u16(uint32_t addr) {
    if (is_mmio_address(addr)) {
        return dpi_mmio_read(addr & ~0x3u) >> ((addr & 0x2u) * 8);
    }

    if (!valid_memory_access(addr, 2)) {
        return 0;
    }

    uint16_t value;
    memcpy(&value, &memory[addr], 2);
    return value;
}

static uint32_t read_u8(uint32_t addr) {
    if (is_mmio_address(addr)) {
        return dpi_mmio_read(addr & ~0x3u) >> ((addr & 0x3u) * 8);
    }

    if (!valid_memory_access(addr, 1)) {
        return 0;
    }

    return memory[addr];
}

static void write_u32(uint32_t addr, uint32_t value) {
    if (is_mmio_address(addr)) {
        dpi_mmio_write(addr, value);
        return;
    }

    if (!valid_memory_access(addr, 4)) {
        return;
    }

    memcpy(&memory[addr], &value, 4);
}

static void write_u16(uint32_t addr, uint16_t value) {
    if (is_mmio_address(addr)) {
        uint32_t base = addr & ~0x3u;
        uint32_t old = dpi_mmio_read(base);
        uint32_t shift = (addr & 0x2u) * 8;
        uint32_t mask = 0xffffu << shift;
        uint32_t merged = (old & ~mask) | ((uint32_t)value << shift);
        dpi_mmio_write(base, merged);
        return;
    }

    if (!valid_memory_access(addr, 2)) {
        return;
    }

    memcpy(&memory[addr], &value, 2);
}

static void write_u8(uint32_t addr, uint8_t value) {
    if (is_mmio_address(addr)) {
        uint32_t base = addr & ~0x3u;
        uint32_t old = dpi_mmio_read(base);
        uint32_t shift = (addr & 0x3u) * 8;
        uint32_t mask = 0xffu << shift;
        uint32_t merged = (old & ~mask) | ((uint32_t)value << shift);
        dpi_mmio_write(base, merged);
        return;
    }

    if (!valid_memory_access(addr, 1)) {
        return;
    }

    memory[addr] = value;
}

static void write_reg(uint32_t reg, uint32_t value) {
    if (reg != 0) {
        regs[reg] = value;
    }
}

/*
 * Compressed instruction (RV32C) decoder.
 *
 * Expands 16-bit compressed instructions into their equivalent RV32I
 * operations. This approach reuses the existing memory access and
 * register write infrastructure without needing a separate decoder.
 *
 * Returns true if the instruction was successfully executed,
 * false if it was an illegal instruction or EBREAK.
 */
static bool execute_compressed(uint16_t insn) {
    /* Decode compressed register (x8-x15) from 3-bit field */
    uint32_t rd_c = ((insn >> 2) & 0x7u) + 8;
    uint32_t rs1_c = ((insn >> 7) & 0x7u) + 8;
    uint32_t rs2_c = ((insn >> 2) & 0x7u) + 8;

    uint32_t funct3 = (insn >> 13) & 0x7u;
    uint32_t funct2 = (insn >> 10) & 0x3u;
    uint32_t funct4 = (insn >> 12) & 0xfu;
    uint32_t rd = (insn >> 7) & 0x1fu;
    uint32_t rs1 = (insn >> 7) & 0x1fu;
    uint32_t rs2 = (insn >> 2) & 0x1fu;

    switch (insn & 0x3u) {
        /*********************************************************************
         * Quadrant 0: insn[1:0] = 0b00
         ********************************************************************/
        case 0x0: {
            switch (funct3) {
                case 0x0: // C.ADDI4SPN (CIW)
                {
                    // nzuimm[9:0] encoded as:
                    // insn[12]=nzuimm[5], insn[11]=nzuimm[4], insn[10]=nzuimm[9],
                    // insn[9]=nzuimm[8],  insn[8]=nzuimm[7],  insn[7]=nzuimm[6],
                    // insn[6]=nzuimm[2],  insn[5]=nzuimm[3]
                    uint32_t nzuimm = ((insn >> 12) & 0x1u) << 5 |  // nzuimm[5]
                                      ((insn >> 11) & 0x1u) << 4 |  // nzuimm[4]
                                      ((insn >> 10) & 0x1u) << 9 |  // nzuimm[9]
                                      ((insn >> 9)  & 0x1u) << 8 |  // nzuimm[8]
                                      ((insn >> 8)  & 0x1u) << 7 |  // nzuimm[7]
                                      ((insn >> 7)  & 0x1u) << 6 |  // nzuimm[6]
                                      ((insn >> 6)  & 0x1u) << 2 |  // nzuimm[2]
                                      ((insn >> 5)  & 0x1u) << 3;   // nzuimm[3]
                    if (nzuimm == 0) return false; // illegal instruction
                    write_reg(rd_c, regs[2] + nzuimm); // sp = x2
                    return true;
                }
                case 0x1: // C.FLD (RV64/DFP) — not supported
                    return false;
                case 0x2: // C.LW (CL)
                {
                    uint32_t offset = ((insn >> 10) & 0x7u) << 3 |
                                      ((insn >> 5) & 0x3u) << 1;
                    uint32_t addr = regs[rs1_c] + offset;
                    write_reg(rd_c, read_u32(addr));
                    return true;
                }
                case 0x3: // C.LD (RV64) — not supported
                    return false;
                case 0x4: // reserved / C.FLW (RV32FC) — not supported
                    return false;
                case 0x5: // C.FSD (RV64/DFP) — not supported
                    return false;
                case 0x6: // C.SW (CS)
                {
                    uint32_t offset = ((insn >> 10) & 0x7u) << 3 |
                                      ((insn >> 5) & 0x3u) << 1;
                    uint32_t addr = regs[rs1_c] + offset;
                    write_u32(addr, regs[rs2_c]);
                    return true;
                }
                case 0x7: // C.SD (RV64) — not supported
                    return false;
                default:
                    return false;
            }
        }

        /*********************************************************************
         * Quadrant 1: insn[1:0] = 0b01
         ********************************************************************/
        case 0x1: {
            switch (funct3) {
                case 0x0: // C.ADDI (CI)
                {
                    uint32_t imm = ((insn >> 12) & 0x1u) << 5 |
                                   ((insn >> 2) & 0x1fu);
                    imm = sign_extend(imm, 6);
                    if (rd == 0) return true; // C.NOP
                    write_reg(rd, regs[rd] + imm);
                    return true;
                }
                case 0x1: // C.JAL (RV32 only) / C.ADDIW (RV64)
                {
                    // C.JAL: rd = x1 (ra), jump with offset
                    uint32_t offset = ((insn >> 3) & 0x7u) << 1 |
                                      ((insn >> 11) & 0x1u) << 4 |
                                      ((insn >> 2) & 0x1u) << 5 |
                                      ((insn >> 7) & 0x1u) << 6 |
                                      ((insn >> 6) & 0x1u) << 7 |
                                      ((insn >> 9) & 0x3u) << 8 |
                                      ((insn >> 8) & 0x1u) << 10 |
                                      ((insn >> 12) & 0x1u) << 11;
                    offset = sign_extend(offset, 12);
                    write_reg(1, pc + 2); // ra = return address
                    pc = pc + offset;
                    return true;
                }
                case 0x2: // C.LI (CI)
                {
                    uint32_t imm = ((insn >> 12) & 0x1u) << 5 |
                                   ((insn >> 2) & 0x1fu);
                    imm = sign_extend(imm, 6);
                    write_reg(rd, imm);
                    return true;
                }
                case 0x3: // C.LUI / C.ADDI16SP (CI)
                {
                    if (rd == 2) { // C.ADDI16SP
                        // imm[9:4] encoded as per RISC-V spec:
                        // insn[12]=imm[9], insn[6]=imm[8], insn[5]=imm[7],
                        // insn[4]=imm[6],  insn[3]=imm[5], insn[2]=imm[4]
                        uint32_t imm = ((insn >> 12) & 0x1u) << 9 |  // imm[9]
                                       ((insn >> 6)  & 0x1u) << 8 |  // imm[8]
                                       ((insn >> 5)  & 0x1u) << 7 |  // imm[7]
                                       ((insn >> 4)  & 0x1u) << 6 |  // imm[6]
                                       ((insn >> 3)  & 0x1u) << 5 |  // imm[5]
                                       ((insn >> 2)  & 0x1u) << 4;   // imm[4]
                        imm = sign_extend(imm, 10);
                        write_reg(2, regs[2] + imm);
                        return true;
                    }
                    if (rd == 0) return true; // hint
                    // C.LUI: 6-bit sign-extended immediate shifted left by 12
                    // imm[5] = insn[12], imm[4:0] = insn[6:2]
                    uint32_t imm = ((insn >> 12) & 0x1u) << 5 |
                                   ((insn >> 2) & 0x1fu);
                    imm = sign_extend(imm, 6);
                    imm = imm << 12;
                    if (imm == 0) return false; // illegal
                    write_reg(rd, imm);
                    return true;
                }
                case 0x4: // reserved (funct3=100 in Q1)
                {
                    // C.SRLI / C.SRAI / C.ANDI / C.SUB / C.XOR / C.OR / C.AND
                    uint32_t funct2_high = (insn >> 10) & 0x3u;
                    uint32_t funct2_low = (insn >> 5) & 0x3u; // bits [6:5] for CA-type

                    if (funct2_high == 0x0) {
                        // C.SRLI (CB) — rd'/rs1' in bits [9:7]
                        uint32_t shamt = ((insn >> 12) & 0x1u) << 5 |
                                         ((insn >> 2) & 0x1fu);
                        if (shamt == 0) return true; // C.NOP hint
                        write_reg(rs1_c, regs[rs1_c] >> shamt);
                        return true;
                    }
                    if (funct2_high == 0x1) {
                        // C.SRAI (CB) — rd'/rs1' in bits [9:7]
                        uint32_t shamt = ((insn >> 12) & 0x1u) << 5 |
                                         ((insn >> 2) & 0x1fu);
                        if (shamt == 0) return true; // C.NOP hint
                        write_reg(rs1_c, (uint32_t)((int32_t)regs[rs1_c] >> shamt));
                        return true;
                    }
                    if (funct2_high == 0x2) {
                        // C.ANDI (CB) — rd'/rs1' in bits [9:7]
                        uint32_t imm = ((insn >> 12) & 0x1u) << 5 |
                                       ((insn >> 2) & 0x1fu);
                        imm = sign_extend(imm, 6);
                        write_reg(rs1_c, regs[rs1_c] & imm);
                        return true;
                    }
                    if (funct2_high == 0x3) {
                        // funct1 is bit [12] for CA-type
                        uint32_t funct1_ca = (insn >> 12) & 0x1u;
                        if (funct1_ca == 0) {
                            // C.SUB / C.XOR / C.OR / C.AND (CA)
                            switch (funct2_low) {
                            case 0x0: // C.SUB
                                write_reg(rs1_c, regs[rs1_c] - regs[rs2_c]);
                                return true;
                            case 0x1: // C.XOR
                                write_reg(rs1_c, regs[rs1_c] ^ regs[rs2_c]);
                                return true;
                            case 0x2: // C.OR
                                write_reg(rs1_c, regs[rs1_c] | regs[rs2_c]);
                                return true;
                            case 0x3: // C.AND
                                write_reg(rs1_c, regs[rs1_c] & regs[rs2_c]);
                                return true;
                            }
                        } else {
                            // C.SUBW / C.ADDW (RV64) — not supported
                            return false;
                        }
                    }
                    return false;
                }
                case 0x5: // C.J (CJ)
                {
                    uint32_t offset = ((insn >> 3) & 0x7u) << 1 |
                                      ((insn >> 11) & 0x1u) << 4 |
                                      ((insn >> 2) & 0x1u) << 5 |
                                      ((insn >> 7) & 0x1u) << 6 |
                                      ((insn >> 6) & 0x1u) << 7 |
                                      ((insn >> 9) & 0x3u) << 8 |
                                      ((insn >> 8) & 0x1u) << 10 |
                                      ((insn >> 12) & 0x1u) << 11;
                    offset = sign_extend(offset, 12);
                    pc = pc + offset;
                    return true;
                }
                case 0x6: // C.BEQZ (CB)
                {
                    uint32_t offset = ((insn >> 3) & 0x3u) << 1 |
                                      ((insn >> 10) & 0x3u) << 3 |
                                      ((insn >> 2) & 0x1u) << 5 |
                                      ((insn >> 5) & 0x1u) << 6 |
                                      ((insn >> 12) & 0x1u) << 8;
                    offset = sign_extend(offset, 9);
                    if (regs[rs1_c] == 0) {
                        pc = pc + offset;
                    }
                    return true;
                }
                case 0x7: // C.BNEZ (CB)
                {
                    uint32_t offset = ((insn >> 3) & 0x3u) << 1 |
                                      ((insn >> 10) & 0x3u) << 3 |
                                      ((insn >> 2) & 0x1u) << 5 |
                                      ((insn >> 5) & 0x1u) << 6 |
                                      ((insn >> 12) & 0x1u) << 8;
                    offset = sign_extend(offset, 9);
                    if (regs[rs1_c] != 0) {
                        pc = pc + offset;
                    }
                    return true;
                }
                default:
                    return false;
            }
        }

        /*********************************************************************
         * Quadrant 2: insn[1:0] = 0b10
         ********************************************************************/
        case 0x2: {
            switch (funct3) {
                case 0x0: // C.SLLI (CI)
                {
                    uint32_t shamt = ((insn >> 12) & 0x1u) << 5 |
                                     ((insn >> 2) & 0x1fu);
                    if (shamt == 0) return true; // C.NOP hint
                    write_reg(rd, regs[rd] << shamt);
                    return true;
                }
                case 0x1: // C.FLDSP (RV64/DFP) — not supported
                    return false;
                case 0x2: // C.LWSP (CI)
                {
                    // offset = {insn[6:5], insn[12], insn[4:2], 2'b00} — 8-bit unsigned, word-aligned
                    uint32_t offset = ((insn >> 5) & 0x3u) << 6 |   // imm[7:6] = insn[6:5]
                                      ((insn >> 12) & 0x1u) << 5 |  // imm[5] = insn[12]
                                      ((insn >> 2) & 0x7u) << 2;    // imm[4:2] = insn[4:2]
                    uint32_t addr = regs[2] + offset; // sp = x2
                    write_reg(rd, read_u32(addr));
                    return true;
                }
                case 0x3: // C.LDSP (RV64) — not supported
                    return false;
                case 0x4: // C.MV / C.ADD / C.JR / C.JALR / C.EBREAK
                {
                    // funct4 = (insn >> 12) & 0xf
                    // bits [15:12] = 1000 → C.MV (rs2 != 0) or C.JR (rs2 == 0)
                    // bits [15:12] = 1001 → C.ADD (rs2 != 0) or C.JALR (rs2 == 0) or C.EBREAK (both 0)
                    uint32_t funct4_val = (insn >> 12) & 0xfu;
                    if (funct4_val == 0x8) {
                        // C.MV or C.JR
                        if (rs2 == 0) {
                            // C.JR rs1 (rs1 != 0)
                            if (rs1 == 0) return false; // illegal
                            pc = regs[rs1];
                            return true;
                        }
                        // C.MV rd, rs2
                        write_reg(rd, regs[rs2]);
                        return true;
                    }
                    if (funct4_val == 0x9) {
                        if (rs2 == 0 && rs1 == 0) {
                            // C.EBREAK
                            return false;
                        }
                        if (rs2 == 0) {
                            // C.JALR rs1
                            if (rs1 == 0) return false; // illegal
                            write_reg(1, pc + 2); // ra = return address
                            pc = regs[rs1];
                            return true;
                        }
                        // C.ADD rd, rs2
                        write_reg(rd, regs[rd] + regs[rs2]);
                        return true;
                    }
                    return false;
                }
                case 0x5: // C.FSDSP (RV64/DFP) — not supported
                    return false;
                case 0x6: // C.SWSP (CSS)
                {
                    // offset[7:6] = insn[11:10], offset[5:4] = insn[6:5]
                    // rs2 = insn[9:7] (different from the default rs2 which is bits [6:2])
                    uint32_t offset = ((insn >> 10) & 0x3u) << 6 |
                                      ((insn >> 5) & 0x3u) << 4;
                    uint32_t rs2_swsp = (insn >> 7) & 0x1fu;
                    uint32_t addr = regs[2] + offset; // sp = x2
                    write_u32(addr, regs[rs2_swsp]);
                    return true;
                }
                case 0x7: // C.SDSP (RV64) — not supported
                    return false;
                default:
                    return false;
            }
        }

        default:
            return false;
    }
}

static bool execute_instruction(uint32_t insn) {
    uint32_t opcode = insn & 0x7fu;
    uint32_t rd = (insn >> 7) & 0x1fu;
    uint32_t funct3 = (insn >> 12) & 0x7u;
    uint32_t rs1 = (insn >> 15) & 0x1fu;
    uint32_t rs2 = (insn >> 20) & 0x1fu;
    uint32_t funct7 = (insn >> 25) & 0x7fu;
    uint32_t imm;
    uint32_t src1 = regs[rs1];
    uint32_t src2 = regs[rs2];
    uint32_t next_pc = pc + 4;

    switch (opcode) {
        case 0x33: // OP (register-register ALU)
            // M-extension: funct7 == 0x01
            if (funct7 == 0x01) {
                switch (funct3) {
                    case 0x0: // MUL
                        write_reg(rd, (uint32_t)((int64_t)(int32_t)src1 * (int64_t)(int32_t)src2));
                        break;
                    case 0x1: // MULH
                        write_reg(rd, (uint32_t)(((int64_t)(int32_t)src1 * (int64_t)(int32_t)src2) >> 32));
                        break;
                    case 0x2: // MULHSU
                        write_reg(rd, (uint32_t)(((int64_t)(int32_t)src1 * (uint64_t)src2) >> 32));
                        break;
                    case 0x3: // MULHU
                        write_reg(rd, (uint32_t)(((uint64_t)src1 * (uint64_t)src2) >> 32));
                        break;
                    case 0x4: // DIV
                        if (src2 == 0) { write_reg(rd, 0xFFFFFFFFu); break; }
                        if ((int32_t)src1 == INT32_MIN && (int32_t)src2 == -1) { write_reg(rd, (uint32_t)INT32_MIN); break; }
                        write_reg(rd, (uint32_t)((int32_t)src1 / (int32_t)src2));
                        break;
                    case 0x5: // DIVU
                        if (src2 == 0) { write_reg(rd, 0xFFFFFFFFu); break; }
                        write_reg(rd, src1 / src2);
                        break;
                    case 0x6: // REM
                        if (src2 == 0) { write_reg(rd, src1); break; }
                        if ((int32_t)src1 == INT32_MIN && (int32_t)src2 == -1) { write_reg(rd, 0); break; }
                        write_reg(rd, (uint32_t)((int32_t)src1 % (int32_t)src2));
                        break;
                    case 0x7: // REMU
                        if (src2 == 0) { write_reg(rd, src1); break; }
                        write_reg(rd, src1 % src2);
                        break;
                }
                pc = next_pc;
                return true;
            }
            // RV32I base OP instructions
            switch (funct3) {
                case 0x0:
                    if (funct7 == 0x20) {
                        write_reg(rd, src1 - src2);
                    } else {
                        write_reg(rd, src1 + src2);
                    }
                    break;
                case 0x1:
                    if (funct7 == 0x00) {
                        write_reg(rd, src1 << (src2 & 0x1f));
                    }
                    break;
                case 0x2:
                    write_reg(rd, (int32_t)src1 < (int32_t)src2 ? 1 : 0);
                    break;
                case 0x3:
                    write_reg(rd, src1 < src2 ? 1 : 0);
                    break;
                case 0x4:
                    write_reg(rd, src1 ^ src2);
                    break;
                case 0x5:
                    if (funct7 == 0x20) {
                        write_reg(rd, (uint32_t)((int32_t)src1 >> (src2 & 0x1f)));
                    } else {
                        write_reg(rd, src1 >> (src2 & 0x1f));
                    }
                    break;
                case 0x6:
                    write_reg(rd, src1 | src2);
                    break;
                case 0x7:
                    write_reg(rd, src1 & src2);
                    break;
                default:
                    break;
            }
            break;
        case 0x13: // OP-IMM
            imm = sign_extend((insn >> 20), 12);
            switch (funct3) {
                case 0x0:
                    write_reg(rd, src1 + imm);
                    break;
                case 0x2:
                    write_reg(rd, (int32_t)src1 < (int32_t)imm ? 1 : 0);
                    break;
                case 0x3:
                    write_reg(rd, src1 < (uint32_t)imm ? 1 : 0);
                    break;
                case 0x4:
                    write_reg(rd, src1 ^ imm);
                    break;
                case 0x6:
                    write_reg(rd, src1 | imm);
                    break;
                case 0x7:
                    write_reg(rd, src1 & imm);
                    break;
                case 0x1:
                    if (((insn >> 25) == 0x00)) {
                        write_reg(rd, src1 << (insn >> 20));
                    }
                    break;
                case 0x5:
                    if ((insn >> 25) == 0x00) {
                        write_reg(rd, src1 >> (insn >> 20));
                    } else if ((insn >> 25) == 0x20) {
                        write_reg(rd, (uint32_t)((int32_t)src1 >> (insn >> 20)));
                    }
                    break;
                default:
                    break;
            }
            break;
        case 0x03: // LOAD
            imm = sign_extend((insn >> 20), 12);
            {
                uint32_t addr = src1 + imm;
                uint32_t data = 0;
                switch (funct3) {
                    case 0x0: // LB
                        data = sign_extend(read_u8(addr), 8);
                        break;
                    case 0x1: // LH
                        data = sign_extend(read_u16(addr), 16);
                        break;
                    case 0x2: // LW
                        data = read_u32(addr);
                        break;
                    case 0x4: // LBU
                        data = read_u8(addr);
                        break;
                    case 0x5: // LHU
                        data = read_u16(addr);
                        break;
                    default:
                        break;
                }
                write_reg(rd, data);
            }
            break;
        case 0x23: // STORE
            imm = ((insn >> 7) & 0x1f) | (((insn >> 25) & 0x7f) << 5);
            imm = sign_extend(imm, 12);
            {
                uint32_t addr = src1 + imm;
                switch (funct3) {
                    case 0x0: // SB
                        write_u8(addr, (uint8_t)src2);
                        break;
                    case 0x1: // SH
                        write_u16(addr, (uint16_t)src2);
                        break;
                    case 0x2: // SW
                        write_u32(addr, src2);
                        break;
                    default:
                        break;
                }
            }
            break;
        case 0x63: // BRANCH
            {
                uint32_t imm_b = ((insn >> 7) & 0x1e) | ((insn >> 25) & 0x3f) << 5 | ((insn >> 7) & 0x1) << 11 | ((insn >> 31) << 12);
                imm_b = sign_extend(imm_b, 13);
                bool taken = false;
                switch (funct3) {
                    case 0x0:
                        taken = regs[rs1] == regs[rs2];
                        break;
                    case 0x1:
                        taken = regs[rs1] != regs[rs2];
                        break;
                    case 0x4:
                        taken = (int32_t)regs[rs1] < (int32_t)regs[rs2];
                        break;
                    case 0x5:
                        taken = (int32_t)regs[rs1] >= (int32_t)regs[rs2];
                        break;
                    case 0x6:
                        taken = regs[rs1] < regs[rs2];
                        break;
                    case 0x7:
                        taken = regs[rs1] >= regs[rs2];
                        break;
                    default:
                        break;
                }
                if (taken) {
                    next_pc = pc + imm_b;
                }
            }
            break;
        case 0x6f: // JAL
            {
                uint32_t imm_j = ((insn >> 21) & 0x3ff) << 1 | ((insn >> 20) & 0x1) << 11 | ((insn >> 12) & 0xff) << 12 | ((insn >> 31) << 20);
                imm_j = sign_extend(imm_j, 21);
                write_reg(rd, pc + 4);
                next_pc = pc + imm_j;
            }
            break;
        case 0x67: // JALR
            {
                uint32_t imm_i = sign_extend((insn >> 20), 12);
                next_pc = (src1 + imm_i) & ~1u;
                write_reg(rd, pc + 4);
            }
            break;
        case 0x37: // LUI
            write_reg(rd, insn & 0xfffff000u);
            break;
        case 0x17: // AUIPC
            write_reg(rd, pc + (insn & 0xfffff000u));
            break;
        case 0x0F: // FENCE / FENCE.I
            // No-ops in single-threaded ISS with no memory ordering
            break;
        case 0x73: // SYSTEM (ECALL, EBREAK, CSR, MRET, WFI)
            if (funct3 == 0) {
                // ECALL (0x00000073) or EBREAK (0x00100073)
                if (insn == 0x00100073u || insn == 0x00000073u) {
                    return false;
                }
                // MRET (0x30200073)
                if (insn == 0x30200073u) {
                    pc = csr_mepc;
                    csr_mstatus |= 0x8; // set MIE
                    return true;
                }
                // WFI (0x10500073) — no-op, handled by polling loop
                break;
            }
            // CSR instructions
            {
                uint32_t csr_addr = (insn >> 20) & 0xFFFu;
                uint32_t csr_val = 0;
                uint32_t uimm = rd; // for CSRWI variants, immediate is in rd field

                // Read current CSR value
                switch (csr_addr) {
                    case 0x300: csr_val = csr_mstatus; break;
                    case 0x305: csr_val = csr_mtvec;   break;
                    case 0x341: csr_val = csr_mepc;    break;
                    case 0x342: csr_val = csr_mcause;  break;
                    case 0xF11: csr_val = 0x00000000u; break; // mvendorid
                    case 0xF12: csr_val = 0x00000001u; break; // marchid
                    case 0xF14: csr_val = 0x00000000u; break; // mhartid
                    default:    csr_val = 0;            break;
                }

                uint32_t new_val = csr_val;
                uint32_t wr_val;

                if (funct3 & 0x4) {
                    // CSR immediate variants (CSRRWI, CSRRSI, CSRRCI)
                    wr_val = uimm;
                } else {
                    // CSR register variants (CSRRW, CSRRS, CSRRC)
                    wr_val = src1;
                }

                switch (funct3) {
                    case 0x1: // CSRRW
                        new_val = wr_val;
                        break;
                    case 0x2: // CSRRS
                        new_val = csr_val | wr_val;
                        break;
                    case 0x3: // CSRRC
                        new_val = csr_val & ~wr_val;
                        break;
                    case 0x5: // CSRRWI
                        new_val = wr_val;
                        break;
                    case 0x6: // CSRRSI
                        new_val = csr_val | wr_val;
                        break;
                    case 0x7: // CSRRCI
                        new_val = csr_val & ~wr_val;
                        break;
                    default:
                        break;
                }

                // Write new CSR value (read-only CSRs skip write)
                switch (csr_addr) {
                    case 0x300: csr_mstatus = new_val; break;
                    case 0x305: csr_mtvec   = new_val; break;
                    case 0x341: csr_mepc    = new_val; break;
                    case 0x342: csr_mcause  = new_val; break;
                    // 0xF11, 0xF12, 0xF14 are read-only — skip write
                    default: break;
                }

                // Write old CSR value to rd (rd=0 means skip)
                write_reg(rd, csr_val);
            }
            break;
        default:
            break;
    }

    pc = next_pc;
    return true;
}

void rv_init(const char *firmware, size_t ram_size) {
    if (memory != NULL) {
        free(memory);
        memory = NULL;
    }

    if (ram_size == 0) {
        ram_size = 1 << 20;
    }

    memory = (uint8_t *)malloc(ram_size);
    if (memory == NULL) {
        memory_size = 0;
        initialized = false;
        return;
    }

    memory_size = (uint32_t)ram_size;
    memset(memory, 0, memory_size);
    memset(regs, 0, sizeof(regs));
    pc = 0;
    irq_mask = 0;
    initialized = true;

    if (firmware != NULL) {
        FILE *file = fopen(firmware, "rb");
        if (file != NULL) {
            size_t bytes = fread(memory, 1, memory_size, file);
            (void)bytes;
            fclose(file);
        }
    }
}

void rv_init_from_buffer(const uint8_t *data, size_t size, size_t ram_size) {
    if (memory != NULL) {
        free(memory);
        memory = NULL;
    }

    if (ram_size == 0) {
        ram_size = 1 << 20;
    }

    memory = (uint8_t *)malloc(ram_size);
    if (memory == NULL) {
        memory_size = 0;
        initialized = false;
        return;
    }

    memory_size = (uint32_t)ram_size;
    memset(memory, 0, memory_size);
    memset(regs, 0, sizeof(regs));
    pc = 0;
    irq_mask = 0;
    initialized = true;

    if (data != NULL && size > 0) {
        size_t copy_size = size < (size_t)ram_size ? size : (size_t)ram_size;
        memcpy(memory, data, copy_size);
    }
}

void rv_reset(uint32_t start_pc) {
    if (!initialized) {
        return;
    }

    memset(regs, 0, sizeof(regs));
    pc = start_pc;
    irq_mask = 0;
}

/* Count trailing zeros (CTZ) — returns position of lowest set bit */
static inline unsigned ctz32(uint32_t x) {
    if (x == 0) return 32;
    unsigned n = 0;
    while ((x & 1) == 0) { n++; x >>= 1; }
    return n;
}

int rv_step(int max_instructions) {
    if (!initialized || max_instructions <= 0) {
        return 0;
    }

    /* Check for pending interrupts at start of batch (only if MIE is set) */
    if (irq_mask != 0 && (csr_mstatus & 0x8)) {
        unsigned cause = ctz32(irq_mask);
        csr_mcause = cause;
        csr_mepc = pc;
        /* Clear the bit we're servicing */
        irq_mask &= ~(1u << cause);
        /* Disable interrupts (clear MIE bit in mstatus) during handler */
        csr_mstatus &= ~0x8u;
        /* Jump to vectored interrupt handler: mtvec + cause * 4 */
        /* If mtvec[0] == 0 (direct mode), all interrupts go to mtvec */
        pc = (csr_mtvec & ~0x3u) + cause * 4;
        return 1;
    }

    int executed = 0;
    for (int i = 0; i < max_instructions; ++i) {
        if (!valid_memory_access(pc, 4)) {
            break;
        }

        uint32_t insn = read_u32(pc);
        if ((insn & 0x3u) != 0x3u) {
            /* 16-bit compressed instruction */
            uint16_t c_insn = (uint16_t)(insn & 0xFFFFu);
            uint32_t pc_before = pc;
            if (!execute_compressed(c_insn)) {
                break;
            }
            /* Jump/branch compressed instructions already update pc.
             * For non-jump instructions (which do not change pc), advance by 2 bytes. */
            if (pc == pc_before) {
                pc += 2;
            }
        } else {
            if (!execute_instruction(insn)) {
                break;
            }
            /* pc is advanced by +4 inside execute_instruction */
        }
        executed += 1;
    }

    return executed;
}

void rv_set_irq(uint32_t mask) {
    irq_mask = mask;
}

void *rv_get_ram(void) {
    return memory;
}

uint32_t rv_get_pc(void) {
    return pc;
}

uint32_t rv_get_reg(unsigned reg) {
    if (reg >= 32) return 0;
    return regs[reg];
}
