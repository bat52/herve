#include "rv32_dpi.h"
#include "herve_profiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern const herve_arch_t* herve_arch_get_by_name(const char *name);

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s run <elf> [options]\n", progname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --arch=<name>      Specify architecture model (e.g., ibex_small, serv, picorv32)\n");
    fprintf(stderr, "  --profile          Enable profiling\n");
    fprintf(stderr, "  --no-profile       Disable profiling (default)\n");
    fprintf(stderr, "  --out=<file>       Output profiling CSV to <file> (default: stdout)\n");
    fprintf(stderr, "  --max-cycles=<n>   Maximum number of cycles to simulate\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    const char *elf_path = argv[2];
    const char *arch_name = "ibex_small";
    bool profiling = false;
    const char *out_file = NULL;
    long long max_cycles = 1000000000; // 1 billion default

    for (int i = 3; i < argc; i++) {
        if (strncmp(argv[i], "--arch=", 7) == 0) {
            arch_name = argv[i] + 7;
        } else if (strcmp(argv[i], "--profile") == 0) {
            profiling = true;
        } else if (strcmp(argv[i], "--no-profile") == 0) {
            profiling = false;
        } else if (strncmp(argv[i], "--out=", 6) == 0) {
            out_file = argv[i] + 6;
        } else if (strncmp(argv[i], "--max-cycles=", 13) == 0) {
            max_cycles = atoll(argv[i] + 13);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    const herve_arch_t *arch = herve_arch_get_by_name(arch_name);
    if (!arch) {
        fprintf(stderr, "Unknown architecture: %s\n", arch_name);
        return 1;
    }

    herve_profiler_set_arch(arch);
    herve_profiler_enable(profiling);

    // rv_init_elf returns the entry point. 0 is a valid entry point.
    // We should probably check if it initialized successfully in some other way,
    // or just assume if it returns a value and we have memory, it's fine.
    rv_init_elf(elf_path, 0); 
    uint32_t entry = rv_get_pc();

    rv_reset(entry);

    int batch_size = 1000;
    while (rv_get_cycles() < (uint64_t)max_cycles && !rv_is_halted()) {
        int steps = rv_step(batch_size);
        if (steps == 0 && !rv_is_halted()) break;
    }

    if (profiling) {
        herve_profiler_report_csv(out_file);
    }

    return rv_get_exit_code();
}
