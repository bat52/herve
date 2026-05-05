/**
 * rv32_dpi_benchmark_htif.cpp — HTIF-based benchmark runner for Herve ISS.
 *
 * This program loads riscv-tests benchmark ELF binaries (e.g. median.riscv)
 * into the Herve ISS, executes them, and detects completion via the HTIF
 * tohost protocol (write to address 0x80001000).
 *
 * riscv-tests benchmarks use the HTIF (Host-Target Interface) protocol:
 *   - tohost is a volatile uint64_t at address 0x80001000 (from test.ld)
 *   - Benchmark signals completion by writing (exit_code << 1) | 1 to tohost
 *   - exit_code == 0 means PASS, non-zero means FAIL
 *
 * Usage:
 *   ./rv32_dpi_benchmark_htif [benchmark_dir] [--csv]
 *     benchmark_dir: path to directory containing .riscv files (default: .)
 *     --csv:         output CSV format only
 *
 * Compile:
 *   g++ -std=c++11 -I./sim/iss -I. -O2 -o rv32_dpi_benchmark_htif \
 *       sim/iss/rv32_dpi_benchmark_htif.cpp sim/iss/rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_benchmark_htif
 *   ./rv32_dpi_benchmark_htif --csv
 *   ./rv32_dpi_benchmark_htif /path/to/benchmarks
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
// HTIF tohost address — defined by test.ld linker script
// -----------------------------------------------------------------------
#define TOHOST_ADDR 0x80001000u

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

#ifndef MAX_INSTRUCTIONS
#define MAX_INSTRUCTIONS 200000000  // 200M max for benchmarks (mm needs more)
#endif
#define MAX_STALE_CHECKS 50000

struct HtifBenchmarkResult {
    std::string name;
    bool passed;
    int instructions;
    double time_sec;       // wall-clock time in seconds
    double ips;            // instructions per second
    std::string reason;
    int exit_code;         // exit code extracted from tohost (0 = pass)
};

// Global RAM buffer — allocated once, reused for all tests
static uint8_t *global_ram = NULL;
static size_t global_ram_size = 0;

/**
 * HTIF syscall handler.
 *
 * The riscv-tests benchmarks use the HTIF (Host-Target Interface) protocol:
 *   - tohost at 0x80001000, fromhost at 0x80001008
 *   - For exit: tohost = (exit_code << 1) | 1  (bit 0 set)
 *   - For syscalls: tohost = pointer to magic_mem[8] structure
 *     magic_mem[0] = syscall number (e.g. 64 = SYS_write)
 *     magic_mem[1] = arg0 (device for SYS_write: 1 = console)
 *     magic_mem[2] = arg1 (pointer to data)
 *     magic_mem[3] = arg2 (command for SYS_write: 1 = putc)
 *
 * Returns the exit code if benchmark completed, -1 if still running.
 */
static int handle_htif(void) {
    if (TOHOST_ADDR + 16 > global_ram_size) {
        return -1;
    }

    // Read tohost value
    uint64_t tohost_val;
    memcpy(&tohost_val, &global_ram[TOHOST_ADDR], sizeof(tohost_val));

    if (tohost_val == 0) {
        return -1;  // No pending HTIF request
    }

    if (tohost_val & 1) {
        // Bit 0 set — benchmark has exited via tohost_exit()
        // tohost = (exit_code << 1) | 1
        return (int)(tohost_val >> 1);
    }

    // tohost is a pointer to magic_mem[8] — process syscall
    uint32_t magic_ptr = (uint32_t)tohost_val;

    // Read syscall info from magic_mem
    uint32_t syscall_no, arg0, arg1, arg2;
    memcpy(&syscall_no, &global_ram[magic_ptr + 0], sizeof(syscall_no));
    memcpy(&arg0,      &global_ram[magic_ptr + 4], sizeof(arg0));
    memcpy(&arg1,      &global_ram[magic_ptr + 8], sizeof(arg1));
    memcpy(&arg2,      &global_ram[magic_ptr + 12], sizeof(arg2));

    // Handle SYS_write to console (putchar)
    if (syscall_no == 64 && arg0 == 1 && arg2 == 1) {
        // Console putchar — arg1 is pointer to character
        char ch;
        memcpy(&ch, &global_ram[arg1], 1);
        putchar(ch);
        fflush(stdout);
    }

    // Acknowledge by writing to fromhost
    // fromhost = (device << 56) | (tag << 16) | (data)
    // For simplicity, just write 1 to fromhost
    uint64_t fromhost_val = 1;
    memcpy(&global_ram[TOHOST_ADDR + 8], &fromhost_val, sizeof(fromhost_val));

    // Clear tohost to indicate request processed
    tohost_val = 0;
    memcpy(&global_ram[TOHOST_ADDR], &tohost_val, sizeof(tohost_val));

    return -1;  // Benchmark still running
}

static int run_htif_benchmark(const uint8_t *elf_data, size_t elf_size,
                               const char *test_name, HtifBenchmarkResult *result) {
    memset(mmio_region, 0, sizeof(mmio_region));

    // Load ELF segments at their correct virtual addresses
    uint32_t entry = rv_load_elf(elf_data, elf_size);
    if (entry == 0) {
        result->passed = false;
        result->reason = "ELF load failed";
        result->instructions = 0;
        result->time_sec = 0;
        result->ips = 0;
        result->exit_code = -1;
        return -1;
    }

    // Reset PC to ELF entry point (also resets registers)
    rv_reset(entry);

    // Timing
    auto start = std::chrono::high_resolution_clock::now();

    int total_executed = 0;
    uint32_t prev_pc = 0;
    int stale_count = 0;
    int exit_code = -1;

    while (total_executed < MAX_INSTRUCTIONS) {
        int executed = rv_step(1000);
        if (executed == 0) {
            // rv_step returned 0 — handle HTIF (syscall or exit)
            exit_code = handle_htif();
            if (exit_code >= 0) {
                break;  // benchmark completed via HTIF exit
            }
            // If not HTIF, it might be ECALL/EBREAK or WFI
            // Check if we're in an infinite loop
            break;
        }
        total_executed += executed;

        // Handle HTIF (process syscalls, check for exit)
        exit_code = handle_htif();
        if (exit_code >= 0) {
            break;  // benchmark completed via HTIF exit
        }

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

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    double time_sec = elapsed.count();

    result->time_sec = time_sec;
    result->instructions = total_executed;
    result->ips = (time_sec > 0) ? (total_executed / time_sec) : 0;

    if (exit_code >= 0) {
        // HTIF exit detected
        result->exit_code = exit_code;
        if (exit_code == 0) {
            result->passed = true;
            result->reason = "PASS (HTIF exit=0)";
            return 0;
        } else {
            result->passed = false;
            char fail_reason[64];
            snprintf(fail_reason, sizeof(fail_reason), "FAIL (HTIF exit=%d)", exit_code);
            result->reason = fail_reason;
            return 0;
        }
    }

    // No HTIF exit detected — timeout or infinite loop
    if (total_executed >= MAX_INSTRUCTIONS) {
        result->passed = false;
        result->reason = "instruction limit";
        result->exit_code = -1;
        return -1;
    }
    if (stale_count >= MAX_STALE_CHECKS) {
        uint32_t stuck_pc = rv_get_pc();
        fprintf(stderr, "DEBUG: stale PC at 0x%08x after %d insn (stale_count=%d)\n",
                stuck_pc, total_executed, stale_count);
        result->passed = false;
        char reason[128];
        snprintf(reason, sizeof(reason), "stale PC (infinite loop) at 0x%08x", stuck_pc);
        result->reason = reason;
        result->exit_code = -1;
        return -1;
    }

    // rv_step returned 0 but no HTIF — possibly ECALL/EBREAK
    // Check tohost one more time
    exit_code = handle_htif();
    if (exit_code >= 0) {
        result->exit_code = exit_code;
        if (exit_code == 0) {
            result->passed = true;
            result->reason = "PASS (HTIF exit=0)";
            return 0;
        } else {
            result->passed = false;
            char fail_reason[64];
            snprintf(fail_reason, sizeof(fail_reason), "FAIL (HTIF exit=%d)", exit_code);
            result->reason = fail_reason;
            return 0;
        }
    }

    result->passed = false;
    result->reason = "unknown termination";
    result->exit_code = -1;
    return -1;
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
    const char *benchmark_dir = ".";
    bool csv_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) {
            csv_mode = true;
        } else {
            benchmark_dir = argv[i];
        }
    }

    if (!csv_mode) {
        printf("=== Herve ISS HTIF Benchmark ===\n\n");
        printf("Benchmark directory: %s\n\n", benchmark_dir);
    }

    // Open benchmark directory
    DIR *dir = opendir(benchmark_dir);
    if (!dir) {
        fprintf(stderr, "ERROR: Cannot open directory '%s'\n", benchmark_dir);
        return 1;
    }

    // Collect .riscv benchmark files
    struct dirent *entry;
    std::vector<std::string> benchmark_names;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        const char *dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".riscv") == 0) {
            benchmark_names.push_back(entry->d_name);
        }
    }
    closedir(dir);

    std::sort(benchmark_names.begin(), benchmark_names.end());

    if (!csv_mode) {
        printf("Found %zu benchmark ELF files\n\n", benchmark_names.size());
    }

    if (benchmark_names.empty()) {
        if (!csv_mode) {
            printf("No .riscv benchmark files found in '%s'\n", benchmark_dir);
        }
        return 0;
    }

    // Allocate global RAM using mmap with MAP_NORESERVE.
    // The riscv-tests benchmark ELF files are linked at 0x80000000, so we need
    // RAM that covers that address plus the tohost area.
    global_ram_size = (size_t)0x80000000 + (16u << 20); // ~2.15 GB virtual
    global_ram = (uint8_t *)mmap(NULL, global_ram_size,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                                  -1, 0);
    if (global_ram == MAP_FAILED) {
        fprintf(stderr, "ERROR: mmap failed for %zu bytes\n", global_ram_size);
        return 1;
    }
    // Tell the ISS to use our pre-allocated buffer
    rv_set_ram(global_ram, global_ram_size);

    // Run each benchmark
    std::vector<HtifBenchmarkResult> results;
    int passed = 0, failed = 0, skipped = 0;

    for (size_t i = 0; i < benchmark_names.size(); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", benchmark_dir, benchmark_names[i].c_str());

        size_t elf_size;
        uint8_t *elf_data = load_file(path, &elf_size);
        if (!elf_data) {
            if (!csv_mode) {
                printf("  [SKIP] %s (cannot load)\n", benchmark_names[i].c_str());
            }
            skipped++;
            continue;
        }

        const std::string &display_name = benchmark_names[i];

        HtifBenchmarkResult result;
        result.name = display_name;
        result.passed = false;
        result.reason = "unknown";
        result.instructions = 0;
        result.time_sec = 0;
        result.ips = 0;
        result.exit_code = -1;

        int ret = run_htif_benchmark(elf_data, elf_size, display_name.c_str(), &result);
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
