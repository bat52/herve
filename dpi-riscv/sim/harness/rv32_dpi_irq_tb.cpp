/**
 * Verilator test harness for IRQ-driven GPIO toggle.
 *
 * This harness demonstrates the full IRQ injection path using WFI:
 *   1. Load IRQ firmware into ISS
 *   2. Run firmware (configures mtvec, enables MIE, enters main loop)
 *   3. Firmware executes WFI — ISS enters sleep state (rv_step returns 0)
 *   4. Assert irq pin (simulating HW peripheral asserting interrupt)
 *   5. SV DPI detects irq, calls rv_set_irq(1)
 *   6. Next rv_step() — ISS wakes from WFI, vectors to interrupt handler
 *   7. Handler reads GPIO_STATUS, toggles GPIO_OUT via MMIO, mret
 *   8. Firmware loops back to WFI — ISS enters sleep again
 *   9. Verify GPIO_OUT toggled via mem_write signal
 *
 * Signal ownership:
 *   clk       - C++ testbench (this file)
 *   rstn      - C++ testbench (this file)
 *   irq       - C++ testbench (this file) — driven high to inject IRQ
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
// -----------------------------------------------------------------------

extern "C" int dpi_mmio_read(int addr) {
    if (addr == 0x10000000) {
        return tb->mem_read;
    }
    if (addr == 0x10000008) {
        // GPIO_STATUS: reflect irq pin state
        return tb->irq ? 1 : 0;
    }
    return 0;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    if (addr == 0x10000000) {
        tb->mem_write = data;
    }
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [firmware.bin]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -n <count>   Number of instructions per step (default: 1000)\n");
    fprintf(stderr, "  -h           Show this help\n");
}

// -----------------------------------------------------------------------
// Clock tick helper
// -----------------------------------------------------------------------
static void tick(VerilatedVcdC *tfp) {
    tb->clk = !tb->clk;
    tb->eval();
    sim_time += CLK_HALF_PERIOD;
    if (tfp) tfp->dump(sim_time);
}

// =======================================================================
// Main
// =======================================================================
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    // Parse arguments
    const char *firmware_path = "firmware_irq.bin";
    int step_batch = 1000;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            step_batch = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            firmware_path = argv[i];
        }
        i++;
    }

    printf("=== RISC-V DPI IRQ Test Harness (WFI) ===\n");
    printf("Firmware: %s\n", firmware_path);
    printf("Step batch: %d\n", step_batch);

    // Create the Verilator model
    tb = new Vtb_top;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5);
    tfp->open("tb_top_irq.vcd");

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
    tb->irq = 0;
    tb->mem_read = 0;
    tb->mem_write = 0;

    printf("Starting simulation...\n");

    // ---- Reset phase: hold rstn low for 2 full clock cycles ----
    tfp->dump(sim_time);
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // De-assert reset
    tb->rstn = 1;

    // ---- Phase 1: Run firmware to configure mtvec, enable MIE, enter main loop ----
    printf("\n--- Phase 1: Boot firmware (configure interrupts, WFI) ---\n");
    int executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions\n", executed);
    printf("PC after boot: 0x%08x\n", rv_get_pc());

    // Tick the RTL clock a few edges to let SV DPI settle
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // ---- Phase 2: Assert IRQ ----
    printf("\n--- Phase 2: Assert IRQ ---\n");
    tb->irq = 1;

    // Tick clock — SV DPI will call rv_set_irq(1) on posedge
    for (int i = 0; i < 2; ++i) {
        tick(tfp);
    }

    // ---- Phase 3: Run ISS — wakes from WFI, vectors to handler ----
    printf("\n--- Phase 3: Execute ISS (wakes from WFI, vectors to handler) ---\n");
    executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions (WFI wake + IRQ vectoring)\n", executed);
    printf("PC after vectoring: 0x%08x\n", rv_get_pc());

    // Tick the RTL clock a few edges
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // ---- Phase 3b: Run ISS again — handler executes and returns to main loop ----
    printf("\n--- Phase 3b: Execute ISS again (handler runs, returns to main loop) ---\n");
    executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions (handler execution + WFI)\n", executed);
    printf("PC after handler: 0x%08x\n", rv_get_pc());

    // Tick the RTL clock a few edges
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // ---- Phase 4: Check results ----
    printf("\n--- Phase 4: Results ---\n");
    printf("GPIO_OUT (mem_write) = 0x%08x\n", tb->mem_write);

    // De-assert IRQ
    tb->irq = 0;
    for (int i = 0; i < 2; ++i) {
        tick(tfp);
    }

    // ---- Phase 5: Post-IRQ execution (should be back in WFI sleep) ----
    printf("\n--- Phase 5: Post-IRQ execution (should be back in WFI sleep) ---\n");
    executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions (should be 0 — WFI sleep)\n", executed);
    printf("PC after post-IRQ step: 0x%08x\n", rv_get_pc());

    // ---- Verify ----
    bool pass = true;

    // GPIO_OUT should have been toggled to 1 by the handler
    if (tb->mem_write == 1) {
        printf("\n[PASS] GPIO_OUT toggled to 1\n");
    } else {
        printf("\n[FAIL] GPIO_OUT = 0x%08x (expected 0x00000001)\n", tb->mem_write);
        pass = false;
    }

    // PC should be back at the WFI instruction (0x20) or the j main_loop (0x24)
    // The main loop is: wfi (0x20) / j main_loop (0x24)
    uint32_t pc = rv_get_pc();
    if (pc == 0x20 || pc == 0x24) {
        printf("[PASS] PC returned to main loop (0x%08x)\n", pc);
    } else {
        printf("[FAIL] PC = 0x%08x (expected 0x20 or 0x24, main loop)\n", pc);
        pass = false;
    }

    printf("\n===========================\n");
    printf("  %s\n", pass ? "PASS" : "FAIL");
    printf("===========================\n");

    // Tear down
    tfp->close();
    tb->final();
    delete tb;
    delete tfp;

    return pass ? 0 : 1;
}
