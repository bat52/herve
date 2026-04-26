#include "rv32_dpi.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MMIO_BASE 0x10000000u
#define MMIO_SIZE 0x00100000u

static uint8_t *memory = NULL;
static uint32_t memory_size = 0;
static uint32_t regs[32];
static uint32_t pc = 0;
static uint32_t irq_mask = 0;
static bool initialized = false;

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
        case 0x33: // OP
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
        case 0x73: // SYSTEM
            if (insn == 0x00100073u || insn == 0x00000073u) {
                return false;
            }
            break;
        default:
            break;
    }

    pc = next_pc;
    return true;
}

void rv_init(const char *firmware, int ram_size) {
    if (memory != NULL) {
        free(memory);
        memory = NULL;
    }

    if (ram_size <= 0) {
        ram_size = 1 << 20;
    }

    memory = (uint8_t *)malloc((size_t)ram_size);
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

void rv_reset(uint32_t start_pc) {
    if (!initialized) {
        return;
    }

    memset(regs, 0, sizeof(regs));
    pc = start_pc;
    irq_mask = 0;
}

int rv_step(int max_instructions) {
    if (!initialized || max_instructions <= 0) {
        return 0;
    }

    int executed = 0;
    for (int i = 0; i < max_instructions; ++i) {
        if (!valid_memory_access(pc, 4)) {
            break;
        }

        uint32_t insn = read_u32(pc);
        if (!execute_instruction(insn)) {
            break;
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
