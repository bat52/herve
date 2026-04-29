/**
 * Verilator test harness for AHB-Lite GPIO DUT with IRQ self-test.
 *
 * This harness demonstrates the full IRQ injection path through the GPIO:
 *   1. Load AHB firmware into ISS
 *   2. Run firmware — it configures interrupts, writes GPIO_OUT=1
 *   3. gpio_out[0] drives ext_irq in Verilog (a Verilog process)
 *   4. Verilog always block calls rv_set_irq(1) on posedge clk
 *   5. Next rv_step() — ISS vectors to interrupt handler
 *   6. Handler reads GPIO_STATUS, clears GPIO_OUT, mret
 *   7. Firmware detects GPIO_OUT==0, writes pass indicator
 *
 * Signal ownership:
 *   clk    - C++ testbench (this file)
 *   rstn   - C++ testbench (this file)
 *   ext_irq - Verilog process (driven from gpio_out[0] via continuous assignment)
 *
 * The BFM request interface is exposed as top-level ports on tb_top_ahb:
 *   tb->ahb_req_valid
 *   tb->ahb_req_addr
 *   tb->ahb_req_write
 *   tb->ahb_req_wdata
 *   tb->ahb_req_ready
 *   tb->ahb_req_rdata
 *
 * DUT register state is read through the exposed module outputs:
 *   tb->gpio_out
 *   tb->gpio_ie
 */

#include "Vtb_top_ahb.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Vtb_top_ahb *tb = nullptr;
static uint64_t sim_time = 0;
static const uint64_t CLK_HALF_PERIOD = 5000; // 5 ns half-period => 100 MHz clock

// Track the pass/fail indicator written by firmware
static int dpi_mmio_read_last_indicator = 0;

// -----------------------------------------------------------------------
// Clock tick helper: toggle clk, eval, dump at proper time increments
// -----------------------------------------------------------------------
static void tick(VerilatedVcdC *tfp) {
    tb->clk = !tb->clk;
    tb->eval();
    sim_time += CLK_HALF_PERIOD;
    if (tfp) tfp->dump(sim_time);
}

// -----------------------------------------------------------------------
// AHB transaction helper: drive BFM request and tick clock until done
// -----------------------------------------------------------------------
static void ahb_transaction(uint32_t addr, bool write, uint32_t wdata,
                            uint32_t *rdata, VerilatedVcdC *tfp) {
    // Drive the request (top-level ports on tb_top_ahb)
    tb->ahb_req_valid = 1;
    tb->ahb_req_addr  = addr;
    tb->ahb_req_write = write ? 1 : 0;
    tb->ahb_req_wdata = wdata;

    // Tick clock until BFM completes (ahb_req_ready asserted)
    int timeout = 100;
    while (timeout > 0) {
        tick(tfp);
        if (tb->ahb_req_ready) {
            break;
        }
        timeout--;
    }

    // Capture read data
    if (rdata) {
        *rdata = tb->ahb_req_rdata;
    }

    // De-assert valid
    tb->ahb_req_valid = 0;

    // Tick until BFM returns to IDLE state
    int idle_timeout = 10;
    while (idle_timeout > 0) {
        tick(tfp);
        if (tb->rootp->tb_top_ahb__DOT__ahb_master__DOT__state == 0) {
            break;
        }
        idle_timeout--;
    }

    if (timeout == 0) {
        fprintf(stderr, "[AHB] TIMEOUT on transaction addr=0x%08x write=%d\n",
                addr, write);
    }
}

// -----------------------------------------------------------------------
// DPI export implementations
//
// These are called by the ISS C code during rv_step().
// They drive the AHB BFM request interface and tick the clock to
// complete the AHB transaction.
// -----------------------------------------------------------------------

extern "C" int dpi_mmio_read(int addr) {
    if (addr == 0x10000100) {
        // Pass/fail indicator — tracked in static variable
        return dpi_mmio_read_last_indicator;
    }

    uint32_t rdata = 0;
    ahb_transaction((uint32_t)addr, false, 0, &rdata, nullptr);
    return (int)rdata;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    // Track the pass/fail indicator at 0x10000100
    if (addr == 0x10000100) {
        dpi_mmio_read_last_indicator = data;
    }

    ahb_transaction((uint32_t)addr, true, (uint32_t)data, nullptr, nullptr);
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

// =======================================================================
// Main
// =======================================================================
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    // Parse arguments
    const char *firmware_path = "firmware_ahb.bin";
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

    printf("=== RISC-V DPI AHB-Lite GPIO IRQ Self-Test ===\n");
    printf("Firmware: %s\n", firmware_path);
    printf("Step batch: %d\n", step_batch);

    // Create the Verilator model
    tb = new Vtb_top_ahb;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5);
    tfp->open("tb_top_ahb.vcd");

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
    // Note: ext_irq is now driven from Verilog (gpio_out[0]), not from C++

    // Initialise BFM request interface (top-level ports)
    tb->ahb_req_valid = 0;
    tb->ahb_req_addr  = 0;
    tb->ahb_req_write = 0;
    tb->ahb_req_wdata = 0;

    printf("Starting simulation...\n");

    // ---- Reset phase: hold rstn low for 2 full clock cycles ----
    tfp->dump(sim_time);
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // De-assert reset
    tb->rstn = 1;

    // Tick a couple edges after reset de-assertion
    for (int i = 0; i < 2; ++i) {
        tick(tfp);
    }

    // ---- Phase 1: Execute firmware boot (mtvec, MIE, IE, GPIO_OUT=1) ----
    printf("\n--- Phase 1: Boot firmware (configure interrupts, write GPIO_OUT=1) ---\n");
    int executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions\n", executed);
    printf("PC after boot: 0x%08x\n", rv_get_pc());
    printf("GPIO_OUT = 0x%08x\n", tb->gpio_out);
    printf("GPIO_IE  = 0x%08x\n", tb->gpio_ie);

    // Tick the RTL clock a few edges to let SV DPI settle
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // ---- Phase 2: Run ISS — should vector to interrupt handler ----
    printf("\n--- Phase 2: Execute ISS (vectors to handler at mtvec = 0x100) ---\n");
    executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions (IRQ vectoring)\n", executed);
    printf("PC after vectoring: 0x%08x\n", rv_get_pc());

    // Tick the RTL clock a few edges
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // ---- Phase 3: Run ISS again — handler reads GPIO_STATUS, clears GPIO_OUT, mret ----
    printf("\n--- Phase 3: Execute ISS (handler runs: read STATUS, clear GPIO_OUT, mret) ---\n");
    executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions (handler execution)\n", executed);
    printf("PC after handler: 0x%08x\n", rv_get_pc());
    printf("GPIO_OUT = 0x%08x\n", tb->gpio_out);

    // Tick the RTL clock a few edges
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }

    // ---- Phase 4: Run ISS again — post-handler: write pass indicator ----
    printf("\n--- Phase 4: Execute ISS (post-handler, write pass indicator) ---\n");
    executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions (post-handler)\n", executed);
    printf("PC after post-handler step: 0x%08x\n", rv_get_pc());

    // ---- Print DUT register state ----
    printf("\n--- AHB GPIO DUT Register State ---\n");
    printf("  GPIO_OUT    = 0x%08x\n", tb->gpio_out);
    printf("  GPIO_IE     = 0x%08x\n", tb->gpio_ie);
    printf("  Pass/Fail   = 0x%08x\n", dpi_mmio_read_last_indicator);

    // ---- Verify expected values ----
    bool pass = true;

    // After the test:
    //   GPIO_OUT should be 0 (cleared by interrupt handler)
    //   GPIO_IE should be 1 (set during boot, not touched by handler)
    //   Pass/Fail indicator should be 1 (success)

    if (tb->gpio_out == 0) {
        printf("\n[PASS] GPIO_OUT = 0x00000000 (cleared by interrupt handler)\n");
    } else {
        printf("\n[FAIL] GPIO_OUT = 0x%08x (expected 0x00000000)\n", tb->gpio_out);
        pass = false;
    }

    if (tb->gpio_ie == 1) {
        printf("[PASS] GPIO_IE  = 0x00000001 (unchanged)\n");
    } else {
        printf("[FAIL] GPIO_IE  = 0x%08x (expected 0x00000001)\n", tb->gpio_ie);
        pass = false;
    }

    if (dpi_mmio_read_last_indicator == 1) {
        printf("[PASS] Firmware completed successfully (pass indicator = 1)\n");
    } else {
        printf("[FAIL] Firmware failed (pass indicator = 0x%08x, expected 1)\n", dpi_mmio_read_last_indicator);
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
