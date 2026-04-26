#include "Vtb_top.h"
#include "Vtb_top___024root.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>

static Vtb_top *tb = nullptr;

static uint32_t make_addi(unsigned rd, unsigned rs1, int32_t imm) {
    return ((uint32_t)(imm & 0xfff) << 20) | (rs1 << 15) | (0 << 12) | (rd << 7) | 0x13u;
}

static uint32_t make_lui(unsigned rd, uint32_t imm20) {
    return (imm20 << 12) | (rd << 7) | 0x37u;
}

static uint32_t make_sw(unsigned rs2, unsigned rs1, int32_t imm) {
    uint32_t imm11_5 = ((uint32_t)imm >> 5) & 0x7fu;
    uint32_t imm4_0 = (uint32_t)imm & 0x1fu;
    return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (2u << 12) | (imm4_0 << 7) | 0x23u;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    tb = new Vtb_top;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5);
    tfp->open("tb_top.vcd");

    rv_init(NULL, 1 << 20);
    uint32_t *ram = (uint32_t *)rv_get_ram();
    ram[0] = make_lui(2, 0x10000u);
    ram[1] = make_addi(1, 0, 0x42);
    ram[2] = make_sw(1, 2, 0);
    ram[3] = 0x0000006fu;

    rv_reset(0);

    tb->rootp->tb_top__DOT__clk = 0;
    tb->rootp->tb_top__DOT__rst = 1;
    tb->rootp->tb_top__DOT__mem_read = 0;
    tb->rootp->tb_top__DOT__mem_write = 0;
    for (int i = 0; i < 2; ++i) {
        tb->rootp->tb_top__DOT__clk = !tb->rootp->tb_top__DOT__clk;
        tb->eval();
    }
    tb->rootp->tb_top__DOT__rst = 0;

    int executed = rv_step(4);
    printf("rv_step executed %d instructions\n", executed);
    printf("RTL MMIO write result = 0x%08x\n", tb->rootp->tb_top__DOT__mem_write);

    for (int i = 0; i < 20; ++i) {
        tb->rootp->tb_top__DOT__clk = !tb->rootp->tb_top__DOT__clk;
        tb->eval();
        tfp->dump(i);
    }

    tfp->close();
    tb->final();
    delete tb;
    delete tfp;
    return (Verilated::gotFinish() ? 0 : 0);
}
