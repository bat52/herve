/**
 * rv32_dpi_irq_test.cpp — Standalone ISS test for IRQ polling.
 *
 * This test builds RV32IM instructions programmatically in the ISS RAM,
 * executes them, injects an interrupt via rv_set_irq(), and verifies
 * that the interrupt handler executes and returns correctly.
 *
 * No Verilator, no SV, no RISC-V toolchain required.
 *
 * Compile:
 *   g++ -I. -o rv32_dpi_irq_test rv32_dpi_irq_test.cpp rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_irq_test
 */

#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// RISC-V instruction helpers (RV32I + CSR)
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

static uint32_t make_lw(unsigned rd, unsigned rs1, int32_t imm) {
    return ((uint32_t)(imm & 0xfff) << 20) | (rs1 << 15) | (2u << 12) | (rd << 7) | 0x03u;
}

static uint32_t make_ebreak(void) {
    return 0x00100073u;
}

static uint32_t make_csrw(unsigned csr_addr, unsigned rs1) {
    // CSRRW rd=0, csr_addr, rs1
    return (csr_addr << 20) | (rs1 << 15) | (1u << 12) | (0 << 7) | 0x73u;
}

static uint32_t make_csrs(unsigned csr_addr, unsigned rs1) {
    // CSRRS rd=0, csr_addr, rs1
    return (csr_addr << 20) | (rs1 << 15) | (2u << 12) | (0 << 7) | 0x73u;
}

static uint32_t make_mret(void) {
    return 0x30200073u;
}

static uint32_t make_jal(unsigned rd, int32_t offset) {
    uint32_t imm = (uint32_t)(offset & 0x1fffffu);
    uint32_t encoded = 0;
    encoded |= ((imm >> 20) & 0x1) << 31;      // imm[20]
    encoded |= ((imm >> 1) & 0x3ff) << 21;     // imm[10:1]
    encoded |= ((imm >> 11) & 0xff) << 12;     // imm[19:12]
    encoded |= ((imm >> 19) & 0x1) << 20;      // imm[11]
    encoded |= (rd << 7);
    encoded |= 0x6fu;
    return encoded;
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
// Test: IRQ-driven GPIO toggle with WFI
//
// Firmware layout:
//   0x0000: _start
//     - t0 = MMIO_BASE (0x1000_0000)
//     - mtvec = 0x0100 (interrupt_handler address)
//     - mstatus.MIE = 1
//     - GPIO_IE = 1
//     - wfi
//     - j main_loop
//
//   0x000C: main_loop
//     - wfi
//     - j main_loop
//
//   0x0100: interrupt_handler
//     - lw t1, 8(t0)   // read GPIO_STATUS
//     - lw t2, 0(t0)   // read GPIO_OUT
//     - xori t2, t2, 1 // toggle bit 0
//     - sw t2, 0(t0)   // write GPIO_OUT
//     - mret
// =======================================================================

static void build_firmware(uint32_t *ram) {
    unsigned a = 0;

    // t0 = MMIO_BASE (0x1000_0000)
    ram[a++] = make_lui(5, 0x10000u);       // lui t0, 0x10000
    ram[a++] = make_addi(5, 5, 0);           // addi t0, t0, 0

    // mtvec = 0x0100 (interrupt handler address)
    ram[a++] = make_addi(6, 0, 0x100);       // t1 = 0x100
    ram[a++] = make_csrw(0x305, 6);          // csrw mtvec, t1

    // mstatus.MIE = 1
    ram[a++] = make_addi(6, 0, 0x8);         // t1 = 0x8 (MIE bit)
    ram[a++] = make_csrs(0x300, 6);          // csrs mstatus, t1

    // GPIO_IE = 1 (MMIO offset 0x04)
    ram[a++] = make_addi(6, 0, 1);           // t1 = 1
    ram[a++] = make_sw(6, 5, 0x004);         // sw t1, 4(t0) — GPIO_IE = 1

    // main_loop: j main_loop (offset = -4 from next PC, so 0)
    // JAL x0, 0 — jump to self
    ram[a++] = make_jal(0, 0);               // j main_loop

    // ================================================================
    // interrupt_handler at address 0x0100
    // ================================================================
    // Pad to 0x0100
    while (a < 0x100 / 4) {
        ram[a++] = 0; // NOP-like padding (actually illegal, but we never execute it)
    }

    // lw t1, 8(t0)   — read GPIO_STATUS
    ram[a++] = make_lw(6, 5, 0x008);
    // lw t2, 0(t0)   — read GPIO_OUT
    ram[a++] = make_lw(7, 5, 0x000);
    // xori t2, t2, 1 — toggle bit 0
    ram[a++] = 0x0013a393u; // xori t2, t2, 1  (opcode 0x13, funct3=0x4, imm=1)
    // sw t2, 0(t0)   — write GPIO_OUT
    ram[a++] = make_sw(7, 5, 0x000);
    // mret
    ram[a++] = make_mret();
}

// =======================================================================
// Main
// =======================================================================
int main(void) {
    printf("=== IRQ Polling Test (Standalone) ===\n\n");

    // Initialize ISS
    rv_init(NULL, 1 << 20);
    uint32_t *ram = (uint32_t *)rv_get_ram();

    // Build firmware
    build_firmware(ram);

    // Print firmware layout
    printf("Firmware at 0x0000: 0x%08x 0x%08x 0x%08x 0x%08x\n",
           ram[0], ram[1], ram[2], ram[3]);
    printf("Firmware at 0x0100: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
           ram[0x100/4], ram[0x100/4+1], ram[0x100/4+2], ram[0x100/4+3], ram[0x100/4+4]);

    // Reset
    rv_reset(0);

    // ---- Phase 1: Boot firmware ----
    printf("\n--- Phase 1: Boot firmware ---\n");
    int executed = rv_step(100);
    printf("rv_step: executed %d instructions\n", executed);
    printf("PC after boot: 0x%08x\n", rv_get_pc());

    // Check that GPIO_OUT is still 0 (not toggled yet)
    printf("GPIO_OUT before IRQ: 0x%08x\n", mmio_region[0]);

    // ---- Phase 2: Inject interrupt ----
    printf("\n--- Phase 2: Inject IRQ ---\n");
    rv_set_irq(1); // IRQ cause 0

    // ---- Phase 3: Execute — first call vectors to handler ----
    printf("\n--- Phase 3: Execute (first call vectors to handler) ---\n");
    executed = rv_step(100);
    printf("rv_step: executed %d instructions (IRQ vectoring)\n", executed);
    printf("PC after vectoring: 0x%08x\n", rv_get_pc());

    // ---- Phase 3b: Execute again — now the handler runs ----
    printf("\n--- Phase 3b: Execute again (handler runs) ---\n");
    executed = rv_step(100);
    printf("rv_step: executed %d instructions (handler execution)\n", executed);
    printf("PC after handler: 0x%08x\n", rv_get_pc());

    // ---- Phase 4: Check results ----
    printf("\n--- Phase 4: Results ---\n");
    printf("GPIO_OUT after IRQ: 0x%08x\n", mmio_region[0]);

    // ---- Verify ----
    bool pass = true;

    // GPIO_OUT should have been toggled to 1 by the handler
    if (mmio_region[0] == 1) {
        printf("[PASS] GPIO_OUT toggled to 1\n");
    } else {
        printf("[FAIL] GPIO_OUT = 0x%08x (expected 0x00000001)\n", mmio_region[0]);
        pass = false;
    }

    // PC should be back in main loop (around 0x20-0x24, the j main_loop instruction)
    uint32_t pc = rv_get_pc();
    if (pc >= 0x20 && pc <= 0x28) {
        printf("[PASS] PC returned to main loop (0x%08x)\n", pc);
    } else {
        printf("[FAIL] PC = 0x%08x (expected ~0x20-0x28, main loop)\n", pc);
        pass = false;
    }

    // ---- Phase 5: Inject another IRQ ----
    printf("\n--- Phase 5: Inject second IRQ ---\n");
    rv_set_irq(1);
    executed = rv_step(100);
    printf("rv_step: executed %d instructions (IRQ vectoring)\n", executed);
    printf("PC after vectoring: 0x%08x\n", rv_get_pc());

    executed = rv_step(100);
    printf("rv_step: executed %d instructions (handler execution)\n", executed);
    printf("PC after handler: 0x%08x\n", rv_get_pc());
    printf("GPIO_OUT after second IRQ: 0x%08x\n", mmio_region[0]);

    if (mmio_region[0] == 0) {
        printf("[PASS] GPIO_OUT toggled back to 0\n");
    } else {
        printf("[FAIL] GPIO_OUT = 0x%08x (expected 0x00000000)\n", mmio_region[0]);
        pass = false;
    }

    printf("\n===========================\n");
    printf("  %s\n", pass ? "PASS" : "FAIL");
    printf("===========================\n");

    return pass ? 0 : 1;
}
