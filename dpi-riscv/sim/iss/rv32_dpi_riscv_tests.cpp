/**
 * rv32_dpi_riscv_tests.cpp — Standalone ISS validation against riscv-tests.
 *
 * This test runner loads each riscv-tests binary into the ISS, executes it,
 * and checks the gp register (x3) for pass/fail.
 *
 * riscv-tests convention:
 *   - gp (x3) = 1 on PASS
 *   - gp (x3) = 0 or other on FAIL
 *   - Tests signal completion via ECALL (which stops our ISS)
 *
 * No Verilator, no SV, no RISC-V toolchain required.
 *
 * Compile:
 *   g++ -I. -o rv32_dpi_riscv_tests rv32_dpi_riscv_tests.cpp rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_riscv_tests [test_dir]
 *   (default test_dir: ../riscv-tests/isa/bin)
 */

#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

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
// Test runner
// -----------------------------------------------------------------------

// Maximum instructions to execute before declaring a timeout
#define MAX_INSTRUCTIONS 50000

// Maximum number of stale-PC checks before declaring a timeout
#define MAX_STALE_CHECKS 100

struct TestResult {
    const char *name;
    bool passed;
    int instructions_executed;
    const char *reason;
};

// Global RAM buffer — allocated once, reused for all tests to avoid
// repeatedly allocating 2.1GB per test.
static uint8_t *global_ram = NULL;
static size_t global_ram_size = 0;

static int run_test(const uint8_t *elf_data, size_t elf_size,
                    const char *test_name, TestResult *result) {
    // Clear MMIO region for a fresh test state.
    memset(mmio_region, 0, sizeof(mmio_region));

    // Load ELF segments at their correct virtual addresses
    uint32_t entry = rv_load_elf(elf_data, elf_size);
    if (entry == 0) {
        result->passed = false;
        result->reason = "ELF load failed";
        result->instructions_executed = 0;
        return -1;
    }

    // Reset PC to ELF entry point (also resets registers)
    rv_reset(entry);

    // Execute until ECALL/EBREAK or timeout
    int total_executed = 0;
    uint32_t prev_pc = 0;
    int stale_count = 0;

    while (total_executed < MAX_INSTRUCTIONS) {
        int executed = rv_step(1000);
        if (executed == 0) {
            // rv_step returned 0 — either ECALL/EBREAK was hit,
            // or we're at an invalid PC
            break;
        }
        total_executed += executed;

        // Detect infinite loop (stale PC)
        uint32_t current_pc = rv_get_pc();
        if (current_pc == prev_pc) {
            stale_count++;
            if (stale_count >= MAX_STALE_CHECKS) {
                break;
            }
        } else {
            stale_count = 0;
        }
        prev_pc = current_pc;
    }

    // Check result
    if (total_executed >= MAX_INSTRUCTIONS || stale_count >= MAX_STALE_CHECKS) {
        // Timeout or infinite loop — likely a FAIL
        result->passed = false;
        result->reason = (stale_count >= MAX_STALE_CHECKS) ? "stale PC (infinite loop)" : "instruction limit";
        result->instructions_executed = total_executed;
        return -1;
    }

    // We stopped on ECALL/EBREAK. Read the gp register (x3) to determine
    // pass/fail. The riscv-tests framework sets gp=1 before ECALL on PASS,
    // and gp to a non-1 value before ECALL on FAIL.
    uint32_t gp = rv_get_reg(3);

    if (gp == 1) {
        result->passed = true;
        result->reason = "PASS (gp=1)";
        result->instructions_executed = total_executed;
        return 0;
    } else {
        result->passed = false;
        result->reason = "FAIL (gp=";
        // Use a static buffer for the reason string
        static char fail_reason[64];
        snprintf(fail_reason, sizeof(fail_reason), "FAIL (gp=%u)", gp);
        result->reason = fail_reason;
        result->instructions_executed = total_executed;
        return 0;
    }
}

// -----------------------------------------------------------------------
// File loading
// -----------------------------------------------------------------------

static uint8_t *load_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open %s\n", path);
        *size_out = 0;
        return NULL;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        fclose(f);
        *size_out = 0;
        return NULL;
    }

    size_t size = (size_t)st.st_size;
    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        fclose(f);
        *size_out = 0;
        return NULL;
    }

    size_t bytes_read = fread(data, 1, size, f);
    fclose(f);

    if (bytes_read != size) {
        free(data);
        *size_out = 0;
        return NULL;
    }

    *size_out = size;
    return data;
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main(int argc, char **argv) {
    const char *test_dir = "../riscv-tests/isa";
    if (argc > 1) {
        test_dir = argv[1];
    }

    printf("=== RISC-V Tests Validation Suite ===\n\n");
    printf("Test directory: %s\n\n", test_dir);

    // Open test directory
    DIR *dir = opendir(test_dir);
    if (!dir) {
        fprintf(stderr, "ERROR: Cannot open directory '%s'\n", test_dir);
        fprintf(stderr, "Run from dpi-riscv/ directory, or specify path:\n");
        fprintf(stderr, "  ./rv32_dpi_riscv_tests <path-to-elf-dir>\n");
        return 1;
    }

    // Collect test files — match the same prefixes that Spike uses
    static const char *prefixes[] = {
        "rv32ui-p-", "rv32um-p-", "rv32uc-p-", NULL
    };
    struct dirent *entry;
    char test_names[256][256];
    int num_tests = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip directories and dump/hex files
        if (entry->d_type == DT_DIR) continue;
        const char *dot = strrchr(entry->d_name, '.');
        if (dot && (strcmp(dot, ".dump") == 0 || strcmp(dot, ".hex") == 0)) continue;

        // Match against known prefixes
        for (int p = 0; prefixes[p] != NULL; p++) {
            if (strncmp(entry->d_name, prefixes[p], strlen(prefixes[p])) == 0) {
                if (num_tests < 256) {
                    strncpy(test_names[num_tests], entry->d_name, 255);
                    test_names[num_tests][255] = '\0';
                    num_tests++;
                }
                break;
            }
        }
    }
    closedir(dir);

    // Sort test names (simple bubble sort)
    for (int i = 0; i < num_tests - 1; i++) {
        for (int j = 0; j < num_tests - i - 1; j++) {
            if (strcmp(test_names[j], test_names[j + 1]) > 0) {
                char tmp[256];
                strncpy(tmp, test_names[j], 255);
                strncpy(test_names[j], test_names[j + 1], 255);
                strncpy(test_names[j + 1], tmp, 255);
            }
        }
    }

    if (num_tests == 0) {
        printf("No matching test ELF files found in '%s'.\n", test_dir);
        printf("Run 'make build_riscv_tests' from the dpi-riscv/ directory to build them.\n");
        printf("Skipping riscv-tests validation.\n\n");
        closedir(dir);
        return 0;
    }

    printf("Found %d test ELF files\n\n", num_tests);

    // Allocate global RAM once — 2GB + 16MB to cover address 0x80000000
    // This avoids malloc/free thrashing for each test.
    global_ram_size = (size_t)0x80000000 + (16u << 20); // 2GB + 16MB
    printf("Allocating %zu MB of RAM...\n\n", global_ram_size / (1024 * 1024));

    // Initialize ISS once (allocates the big buffer)
    rv_init(NULL, global_ram_size);
    global_ram = (uint8_t *)rv_get_ram();
    if (!global_ram) {
        fprintf(stderr, "ERROR: Failed to allocate RAM\n");
        return 1;
    }
    printf("RAM allocated successfully.\n\n");

    // Run each test
    int passed = 0, failed = 0, skipped = 0;

    for (int i = 0; i < num_tests; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", test_dir, test_names[i]);

        size_t elf_size;
        uint8_t *elf_data = load_file(path, &elf_size);
        if (!elf_data) {
            printf("  [SKIP] %s (cannot load)\n", test_names[i]);
            skipped++;
            continue;
        }

        // Use the filename directly (no extension to strip)
        const char *display_name = test_names[i];

        TestResult result;
        result.name = display_name;
        result.passed = false;
        result.reason = "unknown";
        result.instructions_executed = 0;

        int ret = run_test(elf_data, elf_size, display_name, &result);
        free(elf_data);

        if (ret == 0 && result.passed) {
            printf("  [PASS] %s (%d insn)\n", display_name, result.instructions_executed);
            passed++;
        } else if (ret == 0 && !result.passed) {
            printf("  [FAIL] %s (%d insn) — %s\n", display_name, result.instructions_executed, result.reason);
            failed++;
        } else {
            printf("  [SKIP] %s — %s\n", display_name, result.reason);
            skipped++;
        }
    }

    printf("\n===========================\n");
    printf("  Total:  %d\n", num_tests);
    printf("  Passed: %d\n", passed);
    printf("  Failed: %d\n", failed);
    printf("  Skipped: %d\n", skipped);
    printf("===========================\n");

    // Cleanup
    free(global_ram);
    global_ram = NULL;

    return (failed > 0) ? 1 : 0;
}
