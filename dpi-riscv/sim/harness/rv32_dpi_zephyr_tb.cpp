/**
 * Verilator test harness for machine timer with tb_top_zephyr.
 *
 * Tests the full machine timer interrupt path:
 *   1. Load timer firmware into ISS (sets mtimecmp=50000, enables MTIE)
 *   2. Run ISS — firmware enters WFI sleep
 *   3. Tick the RTL clock — SV mtime increments each cycle
 *   4. When mtime >= mtimecmp (50000 cycles), SV asserts timer_irq
 *   5. SV calls rv_set_irq(0x80) on posedge clk
 *   6. Next rv_step() — ISS wakes from WFI, vectors to timer handler
 *   7. Handler toggles GPIO_OUT (mem_write), sets new mtimecmp, mret
 *   8. Firmware loops back to WFI
 *   9. Verify GPIO_OUT toggled via mem_write signal
 *
 * MMIO routing:
 *   GPIO   (0x1000_0000) — handled by C++ dpi_mmio_write -> mem_write port
 *   Timer  (0x1000_0010-1C) — C++ reads mtime_lo/hi ports, writes mtimecmp_lo/hi ports
 *
 * Signal ownership:
 *   clk             - C++ testbench
 *   rstn            - C++ testbench
 *   irq             - C++ testbench (unused external GPIO IRQ)
 *   mem_read        - C++ testbench
 *   mem_write       - firmware via ISS -> dpi_mmio_write()
 *   mtime_lo/hi     - SV output (read by C++ for timer MMIO reads)
 *   mtimecmp_lo/hi  - C++ output (set by C++ on timer MMIO writes)
 */

#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Vtb_top_zephyr *tb = nullptr;
static uint64_t sim_time = 0;
static const uint64_t CLK_HALF_PERIOD = 5000; // 5 ns half-period => 100 MHz clock

// -----------------------------------------------------------------------
// DPI export implementations (direct model access via SV ports)
// -----------------------------------------------------------------------

extern "C" int dpi_mmio_read(int addr) {
    switch (addr) {
        case 0x10000000: return tb->mem_read;                      // GPIO_OUT
        case 0x10000010: return tb->mtime_lo;                      // MTIME_LO
        case 0x10000014: return tb->mtime_hi;                      // MTIME_HI
        case 0x10000018: return tb->mtimecmp_lo;                   // MTIMECMP_LO
        case 0x1000001C: return tb->mtimecmp_hi;                   // MTIMECMP_HI
        default:         return 0;
    }
}

extern "C" void dpi_mmio_write(int addr, int data) {
    switch (addr) {
        case 0x10000000: tb->mem_write = data; break;              // GPIO_OUT
        case 0x10000018: tb->mtimecmp_lo = data; break;            // MTIMECMP_LO
        case 0x1000001C: tb->mtimecmp_hi = data; break;            // MTIMECMP_HI
        default: break;
    }
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [firmware.bin]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -n <count>   Instructions per step (default: 1000)\n");
    fprintf(stderr, "  -c <count>   Clock ticks to run (default: 60000)\n");
    fprintf(stderr, "  -t <count>   Expected timer fire tick (default: 50000)\n");
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
    const char *firmware_path = "firmware_timer.bin";
    int step_batch = 1000;
    int clock_ticks = 80000;
    int expected_tick = 50000;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            step_batch = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            clock_ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            expected_tick = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            firmware_path = argv[i];
        }
        i++;
    }

    printf("=== RISC-V DPI Timer Test Harness ===\n");
    printf("Firmware: %s\n", firmware_path);
    printf("Step batch: %d\n", step_batch);
    printf("Clock ticks: %d\n", clock_ticks);
    printf("Expected timer fire at tick: %d\n", expected_tick);

    // Create the Verilator model
    tb = new Vtb_top_zephyr;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5);
    tfp->open("tb_top_zephyr.vcd");

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
    tb->mtimecmp_lo = 0xFFFFFFFF;
    tb->mtimecmp_hi = 0xFFFFFFFF;

    printf("Starting simulation...\n");

    // ---- Reset phase: hold rstn low for 2 full clock cycles ----
    tfp->dump(sim_time);
    for (int i = 0; i < 4; ++i) {
        tick(tfp);
    }
    // De-assert reset
    tb->rstn = 1;

    // ---- Phase 1: Boot firmware (configure timer, enable MTIE, WFI) ----
    printf("\n--- Phase 1: Boot firmware (configure timer, WFI) ---\n");
    int executed = rv_step(step_batch);
    printf("rv_step: executed %d instructions\n", executed);
    printf("PC after boot: 0x%08x\n", rv_get_pc());

    // Check mtimecmp was written by firmware
    printf("mtimecmp_lo after boot: 0x%08x (expected 0x0000C350)\n", tb->mtimecmp_lo);
    printf("mtimecmp_hi after boot: 0x%08x (expected 0x00000000)\n", tb->mtimecmp_hi);
    printf("mtime_lo after boot: 0x%08x\n", tb->mtime_lo);

    // Check that firmware reached WFI (PC should be at main_loop: 0x28 or 0x2C)
    uint32_t pc = rv_get_pc();
    if (pc == 0x28 || pc == 0x2c) {
        printf("[PASS] Firmware reached main loop (WFI)\n");
    } else {
        printf("[NOTE] PC = 0x%08x (expected ~0x28, WFI main loop)\n", pc);
    }

    // ---- Phase 2: Tick the clock to let mtime reach mtimecmp ----
    printf("\n--- Phase 2: Running clock for %d ticks (waiting for timer) ---\n", clock_ticks);
    printf("GPIO_OUT (mem_write) before timer: 0x%08x\n", tb->mem_write);

    int timer_fired_at = 0;
    int second_fire_at = 0;

    for (int tc = 1; tc <= clock_ticks; tc++) {
        // Tick clock (posedge: mtime++, SV calls rv_set_irq with mtimecmp compare)
        tick(tfp);
        tick(tfp);

        // Let the ISS run — it will execute the handler if an IRQ is pending,
        // then return to WFI sleep. We call rv_step repeatedly until it sleeps.
        // But we limit calls between ticks to avoid infinite spin if WFI can't
        // sleep (e.g. IRQ still asserted from SV side).
        for (int s = 0; s < 5; s++) {
            executed = rv_step(step_batch);
            if (executed == 0) break;  // WFI sleep or nothing to do
        }

        // Check if GPIO_OUT was toggled by the handler
        if (!timer_fired_at && tb->mem_write == 1) {
            timer_fired_at = tc;
            printf("  Timer IRQ fired at tick %d! GPIO_OUT toggled to 1\n", tc);
            printf("  mtime_lo at fire: 0x%08x (dec %u)\n", tb->mtime_lo, tb->mtime_lo);
            printf("  mtimecmp_lo at fire: 0x%08x\n", tb->mtimecmp_lo);
        }

        if (timer_fired_at && tb->mem_write == 0) {
            second_fire_at = tc;
            printf("  Second timer IRQ at tick %d! GPIO_OUT toggled back to 0\n", tc);
        }

        if (second_fire_at) {
            printf("  (stopping early after two timer events)\n");
            break;
        }
    }

    // ---- Phase 3: Check results ----
    printf("\n--- Phase 3: Results ---\n");

    bool pass = true;

    if (timer_fired_at) {
        int delta = timer_fired_at - expected_tick;
        if (delta >= -100 && delta <= 100) {
            printf("[PASS] Timer interrupt fired at tick %d (expected ~%d, delta=%d)\n",
                   timer_fired_at, expected_tick, delta);
        } else {
            printf("[WARN] Timer interrupt fired at tick %d (expected ~%d, delta=%d)\n",
                   timer_fired_at, expected_tick, delta);
        }
    } else {
        printf("[FAIL] Timer interrupt did not fire within %d clock ticks\n", clock_ticks);
        printf("  mem_write = 0x%08x\n", tb->mem_write);
        printf("  mtime_lo at end: 0x%08x (dec %u)\n", tb->mtime_lo, tb->mtime_lo);
        printf("  mtimecmp_lo at end: 0x%08x\n", tb->mtimecmp_lo);
        pass = false;
    }

    if (second_fire_at) {
        printf("[PASS] Periodic timer tick works (second fire at tick %d)\n", second_fire_at);
    } else if (timer_fired_at) {
        printf("[WARN] Only one timer tick observed\n");
    }

    // PC should be back at the WFI instruction (main_loop)
    pc = rv_get_pc();
    if (pc == 0x28 || pc == 0x2c) {
        printf("[PASS] PC returned to main loop (0x%08x)\n", pc);
    } else {
        printf("[INFO] PC = 0x%08x\n", pc);
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
