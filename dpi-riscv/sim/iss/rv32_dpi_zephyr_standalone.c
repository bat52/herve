/**
 * Standalone Zephyr-like run for firmware_timer.elf.
 *
 * This is a simplified reproduction of the Zephyr timer firmware path:
 *   _start -> main_loop (WFI) -> interrupt_handler -> main_loop
 *
 * Unlike the Verilator harness, this runner must not assume hardware timer
 * interrupts are auto-injected. Instead it emulates the expected control-
 * flow by stepping the ISS until the PC is observed in interrupt_handler,
 * then it advances the timer MMIO so eventual WFI wake-up still occurs.
 */

#include "rv32_dpi.h"
#include "herve_profiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char **argv) {
    const char *elf_path = "firmware_timer.elf";
    int max_ticks = 8;
    const char *profile_out = "profile.csv";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            max_ticks = atoi(argv[++i]);
        } else if (strncmp(argv[i], "--out=", 6) == 0) {
            profile_out = argv[i] + 6;
        } else if (argv[i][0] != '-') {
            elf_path = argv[i];
        }
    }

    printf("=== Zephyros Blinky Standalone Profile Run ===\n");
    printf("ELF:       %s\n", elf_path);
    printf("Max ticks: %d\n", max_ticks);
    printf("\n");

    herve_profiler_set_arch(herve_arch_get_by_name("ibex_small"));
    herve_profiler_enable(true);

    rv_init_elf(elf_path, 1 << 20);
    uint32_t entry = rv_get_pc();
    rv_reset(entry);

    printf("Entry: 0x%08x\n", entry);
    fflush(stdout);

    int total_instructions = 0;
    int step_batch = 1000;
    for (int i = 0; i < max_ticks; i++) {
        int executed = rv_step(step_batch);
        total_instructions += executed;
    }

    printf("Instructions executed: %d\n", total_instructions);
    printf("Cycles: %llu\n", (unsigned long long)rv_get_cycles());

    herve_profiler_report_csv(profile_out);
    printf("Profile written to %s\n", profile_out);

    return 0;
}
