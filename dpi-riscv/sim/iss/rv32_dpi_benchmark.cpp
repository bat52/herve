/**
 * rv32_dpi_benchmark.cpp — Benchmark runner for Herve ISS vs Spike comparison.
 *
 * This program loads each riscv-tests binary into the Herve ISS, executes it,
 * and measures execution time. Output is both human-readable and CSV format
 * suitable for comparison with Spike.
 *
 * Usage:
 *   ./rv32_dpi_benchmark [test_dir] [--csv]
 *     test_dir: path to directory containing .bin files (default: ../riscv-tests/isa/bin)
 *     --csv:     output CSV format only
 *
 * Compile:
 *   g++ -std=c++11 -I. -O2 -o rv32_dpi_benchmark rv32_dpi_benchmark.cpp rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_benchmark
 *   ./rv32_dpi_benchmark --csv
 *   ./rv32_dpi_benchmark /path/to/bin/dir
 */

#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

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

#define MAX_INSTRUCTIONS 200000
#define MAX_STALE_CHECKS 200

struct BenchmarkResult {
    std::string name;
    bool passed;
    int instructions;
    double time_sec;       // wall-clock time in seconds
    double ips;            // instructions per second
    std::string reason;
};

// Global RAM buffer — allocated once, reused for all tests
static uint8_t *global_ram = NULL;
static size_t global_ram_size = 0;

static int run_test(const uint8_t *elf_data, size_t elf_size,
                    const char *test_name, BenchmarkResult *result) {
    memset(mmio_region, 0, sizeof(mmio_region));

    // Load ELF segments at their correct virtual addresses
    uint32_t entry = rv_load_elf(elf_data, elf_size);
    if (entry == 0) {
        result->passed = false;
        result->reason = "ELF load failed";
        result->instructions = 0;
        result->time_sec = 0;
        result->ips = 0;
        return -1;
    }

    // Reset PC to ELF entry point (also resets registers)
    rv_reset(entry);

    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    int total_executed = 0;
    uint32_t prev_pc = 0;
    int stale_count = 0;

    while (total_executed < MAX_INSTRUCTIONS) {
        int executed = rv_step(1000);
        if (executed == 0) {
            break;
        }
        total_executed += executed;

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

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    double time_sec = elapsed.count();

    result->time_sec = time_sec;
    result->instructions = total_executed;
    result->ips = (time_sec > 0) ? (total_executed / time_sec) : 0;

    if (total_executed >= MAX_INSTRUCTIONS || stale_count >= MAX_STALE_CHECKS) {
        result->passed = false;
        result->reason = (stale_count >= MAX_STALE_CHECKS) ? "stale PC (infinite loop)" : "instruction limit";
        return -1;
    }

    uint32_t gp = rv_get_reg(3);
    if (gp == 1) {
        result->passed = true;
        result->reason = "PASS";
        return 0;
    } else {
        result->passed = false;
        char fail_reason[64];
        snprintf(fail_reason, sizeof(fail_reason), "FAIL (gp=%u)", gp);
        result->reason = fail_reason;
        return 0;
    }
}

// -----------------------------------------------------------------------
// File loading
// -----------------------------------------------------------------------

static uint8_t *load_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
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
    bool csv_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) {
            csv_mode = true;
        } else {
            test_dir = argv[i];
        }
    }

    if (!csv_mode) {
        printf("=== Herve ISS Benchmark ===\n\n");
        printf("Test directory: %s\n\n", test_dir);
    }

    // Open test directory
    DIR *dir = opendir(test_dir);
    if (!dir) {
        fprintf(stderr, "ERROR: Cannot open directory '%s'\n", test_dir);
        return 1;
    }

    // Collect test files — match the same prefixes that Spike uses
    static const char *prefixes[] = {
        "rv32ui-p-", "rv32um-p-", "rv32uc-p-", NULL
    };
    struct dirent *entry;
    std::vector<std::string> test_names;

    while ((entry = readdir(dir)) != NULL) {
        // Skip directories and dump/hex files
        if (entry->d_type == DT_DIR) continue;
        const char *dot = strrchr(entry->d_name, '.');
        if (dot && (strcmp(dot, ".dump") == 0 || strcmp(dot, ".hex") == 0)) continue;

        // Match against known prefixes
        for (int p = 0; prefixes[p] != NULL; p++) {
            if (strncmp(entry->d_name, prefixes[p], strlen(prefixes[p])) == 0) {
                test_names.push_back(entry->d_name);
                break;
            }
        }
    }
    closedir(dir);

    std::sort(test_names.begin(), test_names.end());

    if (!csv_mode) {
        printf("Found %zu test ELF files\n\n", test_names.size());
    }

    // Allocate global RAM using mmap with MAP_NORESERVE.
    // The riscv-tests ELF files are linked at 0x80000000, so we need RAM that
    // covers that address. Using mmap with MAP_NORESERVE gives us a large
    // virtual address range without committing physical pages (lazy allocation).
    // This avoids the 2GB malloc+memset that causes OOM on constrained systems.
    global_ram_size = (size_t)0x80000000 + (16u << 20); // ~2.15 GB virtual
    global_ram = (uint8_t *)mmap(NULL, global_ram_size,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                                  -1, 0);
    if (global_ram == MAP_FAILED) {
        fprintf(stderr, "ERROR: mmap failed for %zu bytes\n", global_ram_size);
        return 1;
    }
    // Tell the ISS to use our pre-allocated buffer instead of doing its own malloc
    rv_set_ram(global_ram, global_ram_size);

    // Run each test
    std::vector<BenchmarkResult> results;
    int passed = 0, failed = 0, skipped = 0;

    for (size_t i = 0; i < test_names.size(); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", test_dir, test_names[i].c_str());

        size_t elf_size;
        uint8_t *elf_data = load_file(path, &elf_size);
        if (!elf_data) {
            if (!csv_mode) {
                printf("  [SKIP] %s (cannot load)\n", test_names[i].c_str());
            }
            skipped++;
            continue;
        }

        // Use the filename directly (no extension to strip)
        const std::string &display_name = test_names[i];

        BenchmarkResult result;
        result.name = display_name;
        result.passed = false;
        result.reason = "unknown";
        result.instructions = 0;
        result.time_sec = 0;
        result.ips = 0;

        int ret = run_test(elf_data, elf_size, display_name.c_str(), &result);
        free(elf_data);

        results.push_back(result);

        if (ret == 0 && result.passed) {
            if (!csv_mode) {
                printf("  [PASS] %-40s %6d insn  %8.4f s  %12.0f IPS\n",
                       display_name.c_str(), result.instructions,
                       result.time_sec, result.ips);
            }
            passed++;
        } else if (ret == 0 && !result.passed) {
            if (!csv_mode) {
                printf("  [FAIL] %-40s %6d insn  %8.4f s  %12.0f IPS  (%s)\n",
                       display_name.c_str(), result.instructions,
                       result.time_sec, result.ips, result.reason.c_str());
            }
            failed++;
        } else {
            if (!csv_mode) {
                printf("  [SKIP] %-40s %6d insn  %8.4f s  (%s)\n",
                       display_name.c_str(), result.instructions,
                       result.time_sec, result.reason.c_str());
            }
            skipped++;
        }
    }

    // Compute summary
    double total_time = 0;
    int64_t total_insn = 0;
    for (size_t i = 0; i < results.size(); i++) {
        total_time += results[i].time_sec;
        total_insn += results[i].instructions;
    }
    double total_ips = (total_time > 0) ? (total_insn / total_time) : 0;

    // CSV output
    if (csv_mode) {
        // Header
        printf("test_name,passed,instructions,time_sec,ips,reason\n");
        for (size_t i = 0; i < results.size(); i++) {
            printf("%s,%s,%d,%.6f,%.0f,%s\n",
                   results[i].name.c_str(),
                   results[i].passed ? "PASS" : "FAIL",
                   results[i].instructions,
                   results[i].time_sec,
                   results[i].ips,
                   results[i].reason.c_str());
        }
        // Summary line
        printf("TOTAL,%s,%ld,%.6f,%.0f,%d passed, %d failed, %d skipped\n",
               (failed == 0) ? "PASS" : "FAIL",
               (long)total_insn, total_time, total_ips,
               passed, failed, skipped);
    } else {
        printf("\n========================================\n");
        printf("  Total instructions: %ld\n", (long)total_insn);
        printf("  Total time:         %.4f s\n", total_time);
        printf("  Overall IPS:        %.0f\n", total_ips);
        printf("  Passed:  %d\n", passed);
        printf("  Failed:  %d\n", failed);
        printf("  Skipped: %d\n", skipped);
        printf("========================================\n");
    }

    munmap(global_ram, global_ram_size);
    global_ram = NULL;

    return (failed > 0) ? 1 : 0;
}
