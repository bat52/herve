/**
 * Verilator test harness for AHB-Lite GPIO DUT.
 *
 * This harness demonstrates the full AHB-Lite bus transaction path:
 *   1. Load AHB firmware into ISS
 *   2. Run firmware — it performs MMIO reads/writes through the AHB bus
 *   3. Each MMIO access triggers dpi_mmio_read/dpi_mmio_write in C++
 *   4. C++ drives the AHB BFM request interface and ticks the clock
 *   5. After firmware completes, verify DUT register state matches expectations
 *
 * Signal ownership:
 *   clk    - C++ testbench (this file)
 *   rstn   - C++ testbench (this file)
 *   ext_irq - C++ testbench (this file)
 *
 * The BFM request interface is exposed as top-level ports on tb_top_ahb:
 *   tb->ahb_req_valid
 *   tb->ahb_req_addr
 *   tb->ahb_req_write
 *   tb->ahb_req_wdata
 *   tb->ahb_req_ready
 *   tb->ahb_req_rdata
 *
 * The DUT registers are accessed through the Verilator model rootp:
 *   tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_out
 *   tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_ie
 */

#include "Vtb_top_ahb.h"
#include "Vtb_top_ahb___024root.h"
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
    // Use a local tfp pointer — we need to pass nullptr since we don't
    // have access to the tfp from here. The VCD dump will happen in main().
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

    printf("=== RISC-V DPI AHB-Lite GPIO Test Harness ===\n");
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
    tb->ext_irq = 0;

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

    // ---- Execute firmware ----
    printf("\n--- Executing firmware ---\n");
    int executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions\n", executed);
    printf("PC after execution: 0x%08x\n", rv_get_pc());

    // ---- Tick the RTL clock a few more edges ----
    for (int i = 0; i < 10; ++i) {
        tick(tfp);
    }

    // ---- Print DUT register state ----
    printf("\n--- AHB GPIO DUT Register State ---\n");
    printf("  GPIO_OUT    = 0x%08x\n", tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_out);
    printf("  GPIO_IE     = 0x%08x\n", tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_ie);
    printf("  Pass/Fail   = 0x%08x\n", dpi_mmio_read_last_indicator);

    // ---- Verify expected values ----
    bool pass = true;

    // After firmware:
    //   GPIO_OUT should be 0 (cleared in step 6)
    //   GPIO_IE should be 1 (set in step 2)
    //   Pass/Fail indicator should be 1 (success)

    if (tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_out == 0) {
        printf("\n[PASS] GPIO_OUT = 0x00000000 (cleared by firmware)\n");
    } else {
        printf("\n[FAIL] GPIO_OUT = 0x%08x (expected 0x00000000)\n", tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_out);
        pass = false;
    }

    if (tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_ie == 1) {
        printf("[PASS] GPIO_IE  = 0x00000001 (set by firmware)\n");
    } else {
        printf("[FAIL] GPIO_IE  = 0x%08x (expected 0x00000001)\n", tb->rootp->tb_top_ahb__DOT__dut_gpio__DOT__gpio_ie);
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
