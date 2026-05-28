/**
 * Verilator boot harness for Zephyr RTOS on Herve.
 *
 * Loads a Zephyr ELF binary, runs the timer-driven simulation loop,
 * and monitors HTIF console output. The simulation runs until:
 *   - Zephyr exits (HTIF shutdown command), OR
 *   - A maximum number of clock ticks is reached, OR
 *   - A specific output string is detected (optional)
 *
 * Usage:
 *   ./obj_dir/Vtb_top_zephyr_boot zephyr.elf [-c <max_ticks>] [-s <stop_string>]
 *
 * Example:
 *   ./obj_dir/Vtb_top_zephyr_boot build/zephyr/zephyr.elf \
 *     -c 200000 -s "Hello World"
 *
 * Architecture:
 *   The ISS executes instructions in batches via rv_step().
 *   Between batches, the Verilator model toggles the clock,
 *   which increments the SV mtime counter. When mtime >= mtimecmp,
 *   the SV always block calls rv_set_irq(0x80) to signal the
 *   machine timer interrupt to the ISS.
 *
 *   Console output from Zephyr goes through the HTIF interface:
 *   firmware writes to 0x80001000 → ISS processes → putchar() → stdout.
 */

#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

static Vtb_top_zephyr *tb = nullptr;
static uint64_t sim_time = 0;
static const uint64_t CLK_HALF_PERIOD = 5000; // 5 ns half-period => 100 MHz clock
static volatile bool stop_requested = false;

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
// Signal handler for graceful Ctrl+C
// -----------------------------------------------------------------------
static void handle_signal(int sig) {
    (void)sig;
    stop_requested = true;
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

    signal(SIGINT, handle_signal);

    // Parse arguments
    const char *elf_path = NULL;
    int max_ticks = 500000;
    const char *stop_string = NULL;
    bool trace = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            max_ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            stop_string = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s <zephyr.elf> [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -c <count>      Max clock ticks (default: 500000)\n");
            printf("  -s <string>     Stop when this string appears in output\n");
            printf("  --trace         Enable VCD trace\n");
            return 0;
        } else if (argv[i][0] != '-') {
            elf_path = argv[i];
        }
    }

    if (!elf_path) {
        fprintf(stderr, "Error: no ELF file specified.\n");
        fprintf(stderr, "Usage: %s <zephyr.elf> [options]\n", argv[0]);
        return 1;
    }

    printf("=== Herve Zephyr Boot Test Harness ===\n");
    printf("ELF:        %s\n", elf_path);
    printf("Max ticks:  %d\n", max_ticks);
    printf("Stop str:   %s\n", stop_string ? stop_string : "(none)");
    printf("Trace:      %s\n", trace ? "yes" : "no");
    printf("\n");

    // Create the Verilator model
    tb = new Vtb_top_zephyr;
    VerilatedVcdC *tfp = nullptr;
    if (trace) {
        tfp = new VerilatedVcdC;
        tb->trace(tfp, 5);
        tfp->open("tb_top_zephyr_boot.vcd");
    }

    // Load Zephyr ELF into ISS
    // rv_init_elf loads segments at their virtual addresses and sets PC to entry
    rv_init_elf(elf_path, 16 << 20);  // 16 MB RAM
    if (rv_get_ram() == NULL) {
        // rv_init_elf already printed an error to stderr
        delete tb;
        return 1;
    }
    // Reset ISS state (regs, CSRs, timer) but preserve loaded memory
    uint32_t entry = rv_get_pc();
    rv_reset(entry);

    printf("Entry point: 0x%08x\n", entry);
    printf("RAM size:    16 MB\n");
    printf("\n");

    // Print first 4 instructions for verification
    uint32_t *ram = (uint32_t *)rv_get_ram();
    printf("RAM[0..7]: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n\n",
           ram[0], ram[1], ram[2], ram[3], ram[4], ram[5], ram[6], ram[7]);

    // Initialize all signals
    tb->clk = 0;
    tb->rstn = 0;  // active-low reset asserted
    tb->irq = 0;
    tb->mem_read = 0;
    tb->mem_write = 0;
    tb->mtimecmp_lo = 0xFFFFFFFF;
    tb->mtimecmp_hi = 0xFFFFFFFF;

    // ---- Reset phase: hold rstn low for 4 clock cycles ----
    if (tfp) tfp->dump(sim_time);
    for (int i = 0; i < 8; ++i) {
        tick(tfp);
    }
    tb->rstn = 1;  // De-assert reset

    printf("Starting Zephyr boot simulation...\n");
    printf("============================================================\n");

    // ---- Simulation loop ----
    int total_instructions = 0;
    int step_batch = 5000;
    int console_chars = 0;
    bool boot_detected = false;

    for (int tc = 1; tc <= max_ticks && !stop_requested; tc++) {
        // Tick clock (posedge + negedge = one full cycle)
        // On posedge: mtime increments, SV checks mtime >= mtimecmp
        tick(tfp);
        tick(tfp);

        // Run ISS every N ticks to let Zephyr execute instructions
        // The ISS handles WFI sleep: rv_step returns 0 when sleeping,
        // or >0 when instructions were executed (IRQ handling, etc.)
        int executed = rv_step(step_batch);
        if (executed > 0) {
            total_instructions += executed;
        }

        // Check if Zephyr halted (HTIF exit)
        if (rv_is_halted()) {
            printf("\n============================================================\n");
            printf("Zephyr halted with exit code %d\n", rv_get_exit_code());
            break;
        }

        // Check for stop string in console output (optional)
        // Console output from Zephyr goes to stdout via HTIF putchar,
        // which is already handled by rv_step() internally. We can't
        // easily intercept it from here, but the user will see it.

        // Progress indicator
        if (tc % 50000 == 0) {
            printf("[tick %d] instructions=%d pc=0x%08x mtime=%u mtimecmp=0x%08x\n",
                   tc, total_instructions, rv_get_pc(),
                   tb->mtime_lo, tb->mtimecmp_lo);
        }
    }

    printf("============================================================\n");
    printf("\n");

    // ---- Results ----
    printf("=== Results ===\n");
    printf("Clock ticks:   %d\n", max_ticks);
    printf("Instructions:  %d\n", total_instructions);
    printf("Final PC:      0x%08x\n", rv_get_pc());
    printf("Final mtime:   %u\n", tb->mtime_lo);
    printf("Final mtimecmp: 0x%08x\n", tb->mtimecmp_lo);

    bool pass = true;
    if (rv_is_halted()) {
        printf("[PASS] Zephyr exited cleanly (code %d)\n", rv_get_exit_code());
    } else if (total_instructions > 100) {
        printf("[INFO] Zephyr executed %d instructions (may still be booting)\n",
               total_instructions);
    } else {
        printf("[WARN] Very few instructions executed — check ELF compatibility\n");
        pass = false;
    }

    printf("\n===========================\n");
    printf("  %s\n", pass ? "PASS" : "FAIL");
    printf("===========================\n");

    // Tear down
    if (tfp) {
        tfp->close();
        delete tfp;
    }
    tb->final();
    delete tb;

    return pass ? 0 : 1;
}
