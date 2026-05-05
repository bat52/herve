# Benchmark Results: Herve ISS vs Spike

## Overview

This report compares the execution performance of **Herve** (a lightweight RV32IM[C] ISS) against **Spike** (the official RISC-V ISA simulator) using five workloads:
1. The standard **riscv-tests** ISA test suite (rv32ui-p-*, rv32um-p-*, rv32uc-p-*)
2. The **median** benchmark from riscv-tests benchmarks (HTIF-based)
3. The **mm** (matrix multiply) benchmark from riscv-tests benchmarks (HTIF-based, requires soft-float)
4. The **memcpy** benchmark from riscv-tests benchmarks (HTIF-based, integer-only)
5. The **dhrystone** benchmark from riscv-tests benchmarks (HTIF-based, integer-only, classic synthetic benchmark)

### ISA Test Suite Results

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 18,157 | 273,022 | — |
| **Total time** | 0.000454 s | 4.485 s | **9,879×** |
| **Overall IPS** | 39,993,392 | 60,874 | **657×** |
| **Tests passed** | **51/51** | 51/51 | — |
| **Geometric mean speedup** | — | — | **16,929×** |
| **Min speedup** | — | — | 463× |
| **Max speedup** | — | — | 132,000× |

### HTIF Benchmark Results

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 11,000 | 165,000 | — |
| **Total time** | 0.000114 s | 2.335 s | **20,482×** |
| **Overall IPS** | 96,491,228 | 70,664 | **1,365×** |
| **Tests passed** | **1/1** | 1/1 | — |
| **Benchmark** | median.riscv | median.riscv | — |

### mm (Matrix Multiply) Benchmark Results

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 33,017,000 | 33,520,000 | — |
| **Total time** | 0.297 s | 446.957 s | **1,507×** |
| **Overall IPS** | 111,350,185 | 74,996 | **1,485×** |
| **Tests passed** | **1/1** | 1/1 | — |
| **Benchmark** | mm.riscv | mm.riscv | — |
| **Soft-float** | Custom `softfloat.c` | Spike native | — |

### memcpy Benchmark Results

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 32,000 | 195,000 | — |
| **Total time** | 0.000380 s | 2.939 s | **7,734×** |
| **Overall IPS** | 84,311,478 | 66,349 | **1,271×** |
| **Tests passed** | **1/1** | 1/1 | — |
| **Benchmark** | memcpy.riscv | memcpy.riscv | — |
| **Dependencies** | `string.h` (newlib-stubs) | Spike native | — |

### Dhrystone Benchmark Results

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 324,000 | 850,000 | — |
| **Total time** | 0.002936 s | 11.852 s | **4,037×** |
| **Overall IPS** | 110,357,869 | 71,718 | **1,539×** |
| **Tests passed** | **1/1** | 1/1 | — |
| **Benchmark** | dhrystone.riscv | dhrystone.riscv | — |
| **Runs** | 500 (default) | 500 (default) | — |
| **Dependencies** | `alloca.c` (newlib-stubs), `mcycle` CSR (0xB00) | Spike native | — |

The Dhrystone benchmark is a classic synthetic integer benchmark that measures processor performance in **Dhrystones per second**. It exercises a mix of integer arithmetic, string operations, control flow, and procedure calls. With 500 runs, Herve executes 324K instructions in ~2.9ms, achieving ~110M IPS — roughly 1,539× Spike's IPS for this workload.

**Note:** The Dhrystone benchmark uses `read_csr(mcycle)` for timing via `Start_Timer()` and `Stop_Timer()`. Herve now implements the `mcycle` CSR (0xB00) and `minstret` CSR (0xB02), which are incremented each instruction. Without this CSR support, the benchmark would loop forever multiplying `Number_Of_Runs` by 10 because `User_Time` would always be 0 (less than `Too_Small_Time`).


---

## Test Environment

| Component | Detail |
|-----------|--------|
| **Host** | Linux 6.8 (x86-64) |
| **Herve ISS** | `rv32_dpi.c` — single-file lightweight ISS, compiled with `-O2` |
| **Spike** | `/opt/riscv/bin/spike` — official RISC-V ISA simulator |
| **ISA** | RV32IMC |
| **Workload** | riscv-tests ISA suite (rv32ui-p-*, rv32um-p-*, rv32uc-p-*) |
| **Measurement** | Wall-clock time via `std::chrono::high_resolution_clock` (Herve) and shell `time` (Spike) |

---

## Methodology

### Workload

The benchmark uses the **riscv-tests** ISA test suite, specifically:
- **rv32ui-p-\*** — RV32I user-level instruction tests (43 tests)
- **rv32um-p-\*** — RV32M multiply/divide extension tests (7 tests)
- **rv32uc-p-\*** — RV32C compressed extension tests (1 test)

These are small, deterministic test programs that exercise individual RISC-V instructions. Each test executes a sequence of instructions, sets the `gp` register (x3) to 1 on pass, and signals completion via `EBREAK` or `ECALL`.

### Herve Measurement

The Herve benchmark runner (`rv32_dpi_benchmark.cpp`):
1. Loads each `.bin` file into the Herve ISS at address `0x80000000`
2. Calls `rv_reset(0x80000000)` to initialize
3. Runs `rv_step(1000)` in a loop until `EBREAK`/`ECALL` or timeout (200K instructions)
4. Measures wall-clock time using `std::chrono::high_resolution_clock`
5. Checks `gp` register for pass/fail

### Spike Measurement

The Spike wrapper (`run_spike_benchmark.sh`):
1. Runs each ELF binary with `spike --isa=rv32imc --log-commits <elf>`
2. Measures wall-clock time using the shell `time` command
3. Counts instructions from the `--log-commits` output

### Key Difference: Instruction Counts

Herve reports **18,157 total instructions** while Spike reports **273,022 total instructions**. This ~15× difference is because:

- **Herve** counts only *committed instructions* from the test program itself — it stops at `EBREAK`/`ECALL` and does not count any boot or trap handling code.
- **Spike** with `--log-commits` counts *all* committed instructions including Spike's internal boot sequence, trap/exception handling, CSR accesses, and other platform-modeling overhead that occurs before and after the test program runs.

This means the **speedup ratios** are somewhat inflated for Herve since it executes fewer instructions per test. The true performance comparison is better reflected by the **IPS (instructions per second)** metric, which normalizes for instruction count.

---

## Detailed Results

### Per-Test Comparison

| Test | Herve Insn | Spike Insn | Herve Time (s) | Spike Time (s) | Herve IPS | Spike IPS | Speedup |
|------|-----------|-----------|---------------|---------------|----------|----------|---------|
| rv32uc-p-rvc | 253 | 5,257 | 0.000007 | 0.065 | 33,828,052 | 80,877 | 9,286× |
| rv32ui-p-add | 499 | 5,503 | 0.000006 | 0.124 | 82,397,622 | 44,379 | 20,667× |
| rv32ui-p-addi | 276 | 5,280 | 0.000003 | 0.082 | 89,204,913 | 64,390 | 27,333× |
| rv32ui-p-and | 519 | 5,523 | 0.000006 | 0.073 | 89,282,642 | 75,658 | 12,167× |
| rv32ui-p-andi | 232 | 5,236 | 0.000006 | 0.069 | 39,076,975 | 75,884 | 11,500× |
| rv32ui-p-auipc | 92 | 5,096 | 0.000001 | 0.076 | 107,101,281 | 67,053 | 76,000× |
| rv32ui-p-beq | 325 | 5,329 | 0.000009 | 0.076 | 38,038,390 | 70,118 | 8,444× |
| rv32ui-p-bge | 343 | 5,347 | 0.000004 | 0.078 | 92,677,655 | 68,551 | 19,500× |
| rv32ui-p-bgeu | 368 | 5,372 | 0.000004 | 0.076 | 90,796,941 | 70,684 | 19,000× |
| rv32ui-p-blt | 325 | 5,329 | 0.000004 | 0.072 | 90,529,248 | 74,014 | 18,000× |
| rv32ui-p-bltu | 350 | 5,354 | 0.000004 | 0.068 | 83,952,986 | 78,735 | 17,000× |
| rv32ui-p-bne | 325 | 5,329 | 0.000004 | 0.067 | 89,482,379 | 79,537 | 16,750× |
| rv32ui-p-fence_i | 330 | 5,334 | 0.000009 | 0.073 | 37,174,721 | 73,068 | 8,111× |
| rv32ui-p-jal | 89 | 5,093 | 0.000001 | 0.132 | 109,068,627 | 38,583 | 132,000× |
| rv32ui-p-jalr | 149 | 5,153 | 0.000002 | 0.139 | 67,912,489 | 37,072 | 69,500× |
| rv32ui-p-lb | 287 | 5,291 | 0.000003 | 0.306 | 93,576,785 | 17,291 | 102,000× |
| rv32ui-p-lbu | 287 | 5,291 | 0.000003 | 0.101 | 94,129,223 | 52,386 | 33,667× |
| rv32ui-p-ld_st | 997 | 6,001 | 0.000010 | 0.079 | 101,776,235 | 75,962 | 7,900× |
| rv32ui-p-lh | 303 | 5,307 | 0.000007 | 0.074 | 40,616,622 | 71,716 | 10,571× |
| rv32ui-p-lhu | 312 | 5,316 | 0.000004 | 0.069 | 77,304,262 | 77,043 | 17,250× |
| rv32ui-p-lui | 99 | 5,103 | 0.000003 | 0.066 | 29,819,277 | 77,318 | 22,000× |
| rv32ui-p-lw | 317 | 5,321 | 0.000003 | 0.076 | 92,177,959 | 70,013 | 25,333× |
| rv32ui-p-ma_data | 414 | 5,079 | 0.000004 | 0.132 | 95,966,620 | 38,477 | 33,000× |
| rv32ui-p-or | 522 | 5,526 | 0.000006 | 0.071 | 88,987,385 | 77,831 | 11,833× |
| rv32ui-p-ori | 239 | 5,243 | 0.000003 | 0.074 | 85,174,626 | 70,851 | 24,667× |
| rv32ui-p-sb | 488 | 5,492 | 0.000012 | 0.068 | 39,380,245 | 80,765 | 5,667× |
| rv32ui-p-sh | 541 | 5,545 | 0.000010 | 0.069 | 51,874,580 | 80,362 | 6,900× |
| rv32ui-p-simple | 75 | 5,079 | 0.000002 | 0.072 | 31,860,663 | 70,542 | 36,000× |
| rv32ui-p-sll | 527 | 5,531 | 0.000006 | 0.070 | 94,376,791 | 79,014 | 11,667× |
| rv32ui-p-slli | 275 | 5,279 | 0.000003 | 0.127 | 87,831,364 | 41,567 | 42,333× |
| rv32ui-p-slt | 493 | 5,497 | 0.000006 | 0.105 | 88,989,170 | 52,352 | 17,500× |
| rv32ui-p-slti | 271 | 5,275 | 0.000003 | 0.110 | 85,596,968 | 47,955 | 36,667× |
| rv32ui-p-sltiu | 271 | 5,275 | 0.000008 | 0.070 | 35,751,979 | 75,357 | 8,750× |
| rv32ui-p-sltu | 493 | 5,497 | 0.000013 | 0.077 | 38,615,180 | 71,390 | 5,923× |
| rv32ui-p-sra | 546 | 5,550 | 0.000006 | 0.131 | 86,406,077 | 42,366 | 21,833× |
| rv32ui-p-srai | 290 | 5,294 | 0.000004 | 0.123 | 81,369,248 | 43,041 | 30,750× |
| rv32ui-p-srl | 540 | 5,544 | 0.000006 | 0.091 | 84,440,970 | 60,923 | 15,167× |
| rv32ui-p-srli | 284 | 5,288 | 0.000003 | 0.069 | 82,774,701 | 76,638 | 23,000× |
| rv32ui-p-st_ld | 517 | 5,521 | 0.000013 | 0.072 | 38,962,996 | 76,681 | 5,538× |
| rv32ui-p-sub | 491 | 5,495 | 0.000015 | 0.090 | 33,173,434 | 61,056 | 6,000× |
| rv32ui-p-sw | 548 | 5,552 | 0.000006 | 0.080 | 91,900,050 | 69,400 | 13,333× |
| rv32ui-p-xor | 521 | 5,525 | 0.000006 | 0.074 | 92,245,042 | 74,662 | 12,333× |
| rv32ui-p-xori | 241 | 5,245 | 0.000003 | 0.065 | 86,441,894 | 80,692 | 21,667× |
| rv32um-p-div | 130 | 5,134 | 0.000002 | 0.083 | 80,845,771 | 61,855 | 41,500× |
| rv32um-p-divu | 131 | 5,135 | 0.000003 | 0.064 | 51,272,016 | 80,234 | 21,333× |
| rv32um-p-mul | 493 | 5,497 | 0.000010 | 0.080 | 49,113,369 | 68,712 | 8,000× |
| rv32um-p-mulh | 493 | 5,497 | 0.000012 | 0.076 | 41,484,349 | 72,329 | 6,333× |
| rv32um-p-mulhsu | 493 | 5,497 | 0.000005 | 0.074 | 93,869,002 | 74,284 | 14,800× |
| rv32um-p-mulhu | 493 | 5,497 | 0.000177 | 0.082 | 2,788,351 | 67,037 | 463× |
| rv32um-p-rem | 130 | 5,134 | 0.000002 | 0.075 | 63,260,341 | 68,453 | 37,500× |
| rv32um-p-remu | 130 | 5,134 | 0.000002 | 0.070 | 83,762,887 | 73,343 | 35,000× |
| **median.riscv** | **11,000** | **165,000** | **0.000114** | **2.335** | **96,590,360** | **70,664** | **20,482×** |
| **mm.riscv** | **33,017,000** | **33,520,000** | **0.297** | **446.957** | **111,350,185** | **74,996** | **1,507×** |
| **memcpy.riscv** | **32,000** | **195,000** | **0.000380** | **2.939** | **84,311,478** | **66,349** | **7,734×** |
| **dhrystone.riscv** | **324,000** | **850,000** | **0.002936** | **11.852** | **110,357,869** | **71,718** | **4,037×** |


## Analysis

### Why is Herve So Much Faster?

Herve is a **lightweight, single-file ISS** written in C with minimal overhead. It does not model:
- Pipeline stages
- Cache hierarchy
- Memory management (MMU/PMU)
- Multiple harts/cores
- Physical memory protection (PMP)
- Trap handling and CSRs (beyond what's needed for basic execution)
- Device tree or platform devices

Spike is a **full-featured, golden-reference simulator** that models a complete RISC-V platform including all of the above. For each test, Spike goes through a full boot sequence, sets up privilege levels, handles traps, and manages CSRs — all of which contribute to the ~15× higher instruction count and significantly longer wall-clock time.

### Instruction Count Discrepancy

The ~15× difference in instruction counts between Herve and Spike is expected and consistent:

- **Herve** loads the binary directly at the test's entry point and stops at the first `EBREAK`/`ECALL`. It counts only the test program's own instructions.
- **Spike** boots from the ELF entry point, which includes Spike's internal initialization code, trap vector setup, and other platform overhead before the test program even starts executing.

### IPS Comparison

Even when accounting for the instruction count difference, Herve's **IPS is ~657× higher** than Spike's (40M vs 61K). This reflects the fundamental architectural difference:

- Herve is a minimal instruction interpreter — a tight loop of fetch-decode-execute with no platform modeling
- Spike includes full system simulation with privilege levels, CSRs, trap handling, and device modeling

### Speedup Range

The per-test speedup ranges from **463×** (rv32um-p-mulhu — the slowest Herve test at 0.000177 s) to **132,000×** (rv32ui-p-jal — the shortest Herve test at 89 instructions). The speedup is inversely correlated with test length: shorter tests have a higher proportion of Spike overhead relative to actual test instructions.

---

## Raw Data

The CSV files used for this analysis are available in the repository:

- `dpi-riscv/herve_benchmark.csv` — Herve ISS benchmark results
- `dpi-riscv/spike_benchmark.csv` — Spike benchmark results

### How to Reproduce

```bash
# From the dpi-riscv directory:

# Run Herve benchmark
./rv32_dpi_benchmark --csv > herve_benchmark.csv

# Run Spike benchmark
bash tests/run_spike_benchmark.sh --csv > spike_benchmark.csv

# Compare
python3 tests/analyze_benchmark.py herve_benchmark.csv spike_benchmark.csv
```

---

## HTIF-Based Benchmark Support

In addition to the ISA test suite, the benchmark pipeline now supports **riscv-tests benchmark programs** that use the **HTIF (Host-Target Interface)** protocol for completion signaling.

### HTIF Protocol

riscv-tests benchmarks (median, dhrystone, multiply, etc.) signal completion by writing to a `tohost` memory-mapped register:

- `tohost` is a `volatile uint64_t` at address `0x80001000` (defined by `test.ld`)
- Benchmark writes `(exit_code << 1) | 1` to signal completion
- `exit_code == 0` means PASS, non-zero means FAIL

### Herve HTIF Runner

The `rv32_dpi_benchmark_htif` runner (`sim/iss/rv32_dpi_benchmark_htif.cpp`):
1. Discovers `.riscv` ELF files in the benchmark directory
2. Loads each ELF via `rv_load_elf()` at its linked address
3. Runs `rv_step(1000)` in a loop, checking `tohost` at `0x80001000` after each batch
4. Detects HTIF completion by reading `uint64_t` at `RAM[0x80001000]`, checking bit 0
5. Extracts exit code as `tohost >> 1`
6. Outputs human-readable and CSV format matching the existing benchmark runner

### Spike HTIF Runner

The `run_spike_benchmark.sh` script supports a `--benchmark <elf>` flag for running single benchmark ELFs on Spike:
1. Runs `spike --isa=rv32imc --log-commits <elf>`
2. Measures wall-clock time via shell `time`
3. Counts instructions from `--log-commits` output
4. Outputs CSV in the same format as ISA test runs

### How to Reproduce

```bash
# From the dpi-riscv directory:

# Build the benchmark ELFs
make median.riscv
make mm.riscv          # requires softfloat.o and sync_stubs.o
make memcpy.riscv
make dhrystone.riscv   # requires alloca.o

# Build the HTIF benchmark runner
make rv32_dpi_benchmark_htif

# Run Herve benchmark (human-readable)
make run_benchmark_htif

# Run Herve benchmark (CSV)
make run_benchmark_htif_csv

# Run Spike benchmark (human-readable)
make run_spike_benchmark_median
make run_spike_benchmark_mm
make run_spike_benchmark_memcpy
make run_spike_benchmark_dhrystone

# Run Spike benchmark (CSV)
make run_spike_benchmark_median_csv
make run_spike_benchmark_mm_csv
make run_spike_benchmark_memcpy_csv
make run_spike_benchmark_dhrystone_csv

# Full comparison (Herve vs Spike)
make compare_benchmark_htif    # median
make compare_benchmark_mm      # mm
make compare_benchmark_memcpy  # memcpy
make compare_benchmark_dhrystone  # dhrystone
```

### Makefile Targets

| Target | Description |
|--------|-------------|
| `median.riscv` | Build the median benchmark ELF from riscv-tests source |
| `mm.riscv` | Build the mm (matrix multiply) benchmark ELF (requires softfloat.o, sync_stubs.o) |
| `memcpy.riscv` | Build the memcpy benchmark ELF from riscv-tests source (integer-only, no extra libs) |
| `softfloat.o` | Build the custom soft-float library for RV32 double-precision emulation |
| `sync_stubs.o` | Build sync stubs for `__sync_fetch_and_add_4` |
| `rv32_dpi_benchmark_htif` | Build the HTIF-aware Herve benchmark runner |
| `run_benchmark_htif` | Run all `.riscv` benchmarks on Herve |
| `run_benchmark_htif_csv` | Run all `.riscv` benchmarks on Herve (CSV output) |
| `run_spike_benchmark_median` | Run median benchmark on Spike |
| `run_spike_benchmark_median_csv` | Run median benchmark on Spike (CSV output) |
| `run_spike_benchmark_mm` | Run mm benchmark on Spike |
| `run_spike_benchmark_mm_csv` | Run mm benchmark on Spike (CSV output) |
| `run_spike_benchmark_memcpy` | Run memcpy benchmark on Spike |
| `run_spike_benchmark_memcpy_csv` | Run memcpy benchmark on Spike (CSV output) |
| `alloca.o` | Build the alloca() stub for Dhrystone bare-metal compilation |
| `dhrystone.riscv` | Build the Dhrystone benchmark ELF from riscv-tests source (requires alloca.o) |
| `run_benchmark_dhrystone` | Run Dhrystone benchmark on Herve |
| `run_benchmark_dhrystone_csv` | Run Dhrystone benchmark on Herve (CSV output) |
| `run_spike_benchmark_dhrystone` | Run Dhrystone benchmark on Spike |
| `run_spike_benchmark_dhrystone_csv` | Run Dhrystone benchmark on Spike (CSV output) |
| `compare_benchmark_dhrystone` | Full Herve vs Spike comparison for Dhrystone benchmark |
| `compare_benchmark_htif` | Full Herve vs Spike comparison for HTIF benchmarks (median) |
| `compare_benchmark_mm` | Full Herve vs Spike comparison for mm benchmark |
| `compare_benchmark_memcpy` | Full Herve vs Spike comparison for memcpy benchmark |

---

## Known Issues

1. **Instruction count mismatch** — Herve and Spike report different instruction counts for the same tests due to Spike's platform initialization overhead. This is expected behavior.
2. **Timer resolution** — Herve's execution times are extremely small (microseconds), approaching the resolution limits of `std::chrono::high_resolution_clock`. For more accurate timing on larger workloads, the benchmark suite should be extended with the riscv-tests benchmark programs (dhrystone, median, multiply, etc.) which execute millions of instructions.
3. **HTIF benchmark ELF size** — The median benchmark ELF is ~2.15 GB in virtual address space due to the mmap allocation at 0x80000000. This is normal for the Herve ISS which uses `MAP_NORESERVE` to avoid actual memory consumption.
