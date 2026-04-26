/**
 * Verilator test harness for tb_top with binary firmware loading.
 *
 * This harness:
 *   1. Accepts a firmware binary path as command-line argument (default: firmware.bin)
 *   2. Loads the firmware into the ISS via rv_init()
 *   3. Steps through a configurable number of instructions
 *   4. Captures any MMIO writes from the ISS back into the SV DPI exports
 *   5. Displays the result
 *
 * The ISS C code (rv32_dpi.c) calls dpi_mmio_read() and dpi_mmio_write()
 * as regular C functions. We provide those functions here, implementing
 * them by directly accessing the Verilator model's signals rather than
 * going through Verilator's DPI export mechanism (which requires scope
 * management that's problematic when called from outside eval context).
 *
 * Signal ownership:
 *   clk       - C++ testbench (this file)
 *   rstn      - C++ testbench (this file)
 *   mem_read  - C++ testbench (this file)
 *   mem_write - firmware via ISS -> dpi_mmio_write() -> SV export
 */

#include "Vtb_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Vtb_top *tb = nullptr;
static uint64_t sim_time = 0;
static const uint64_t CLK_HALF_PERIOD = 5000; // 5 ns half-period => 100 MHz clock

// -----------------------------------------------------------------------
// DPI export implementations (direct model access)
//
// Instead of routing through Verilator's DPI export dispatch (which
// requires proper scope context), we directly read/write the SV signals
// through the Verilator model's port interface.
// -----------------------------------------------------------------------

extern "C" int dpi_mmio_read(int addr) {
    if (addr == 0x10000000) {
        return tb->mem_read;
    }
    return 0;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    if (addr == 0x10000000) {
        tb->mem_write = data;
    }
}

// -----------------------------------------------------------------------
// DPI import: called by SV tb_top.sv to load firmware into ISS
// -----------------------------------------------------------------------

extern "C" void tb_load_firmware(const char *firmware_path) {
    if (!firmware_path || !*firmware_path) {
        fprintf(stderr, "tb_load_firmware: no firmware path given\n");
        return;
    }
    printf("tb_load_firmware: loading '%s'\n", firmware_path);

    // Re-init the ISS with the firmware binary
    rv_init(firmware_path, 1 << 20);

    uint32_t *ram = (uint32_t *)rv_get_ram();
    printf("  Firmware loaded, first 4 words: 0x%08x 0x%08x 0x%08x 0x%08x\n",
           ram[0], ram[1], ram[2], ram[3]);

    // Reset PC to 0 after loading
    rv_reset(0);
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [firmware.bin]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -n <count>   Number of instructions to execute (default: 64)\n");
    fprintf(stderr, "  -h           Show this help\n");
}

// -----------------------------------------------------------------------
// Clock tick helper: toggle clk, eval, dump at proper time increments
// -----------------------------------------------------------------------
static void tick(VerilatedVcdC *tfp) {
    tb->clk = !tb->clk;
    tb->eval();
    sim_time += CLK_HALF_PERIOD;
    tfp->dump(sim_time);
}

// =======================================================================
// Main
// =======================================================================
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    // Parse arguments
    const char *firmware_path = "firmware.bin";
    int max_instructions = 64;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_instructions = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            firmware_path = argv[i];
        }
        i++;
    }

    printf("=== RISC-V DPI Test Harness ===\n");
    printf("Firmware: %s\n", firmware_path);
    printf("Max instructions: %d\n", max_instructions);

    // Create the Verilator model
    tb = new Vtb_top;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5);
    tfp->open("tb_top.vcd");

    // Load firmware into ISS
    rv_init(firmware_path, 1 << 20);
    rv_reset(0);

    // Print first few words for verification
    uint32_t *ram = (uint32_t *)rv_get_ram();
    printf("RAM[0..7]: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
           ram[0], ram[1], ram[2], ram[3], ram[4], ram[5], ram[6], ram[7]);

    // Initialise all signals
    tb->clk = 0;
    tb->rstn = 0;  // active-low reset asserted
    tb->mem_read = 0;
    tb->mem_write = 0;

    printf("Starting simulation...\n");

    // ---- Reset phase: hold rstn low for 2 full clock cycles ----
    // Dump initial state at time 0
    tfp->dump(sim_time);
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // De-assert reset
    tb->rstn = 1;

    // ---- Drive mem_read with known patterns from C++ ----
    tb->mem_read = 0xA5A5A5A5;

    // Tick a couple edges with this pattern
    for (int i = 0; i < 2; ++i) {
        tick(tfp);
    }

    // Change mem_read pattern
    tb->mem_read = 0x5A5A5A5A;

    for (int i = 0; i < 2; ++i) {
        tick(tfp);
    }

    // Set final mem_read value for firmware execution
    tb->mem_read = 0xDEADBEEF;

    // ---- Execute firmware ----
    printf("Starting firmware execution...\n");
    int executed = rv_step(max_instructions);
    printf("rv_step: executed %d instructions\n", executed);

    // ---- Tick the RTL clock a few more edges ----
    for (int i = 0; i < 10; ++i) {
        tick(tfp);
    }

    // ---- Print results ----
    printf("\nRTL MMIO write result = 0x%08x\n", tb->mem_write);

    // Tear down
    tfp->close();
    tb->final();
    delete tb;
    delete tfp;

    printf("=== Done ===\n");
    return 0;
}
