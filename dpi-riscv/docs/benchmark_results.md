# Benchmark Results: Herve ISS vs Spike

## Overview

This report compares the execution performance of **Herve** (a lightweight RV32IM[C] ISS) against **Spike** (the official RISC-V ISA simulator) using the standard **riscv-tests** ISA test suite as the workload.

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 18,157 | 273,022 | — |
| **Total time** | 0.000454 s | 4.485 s | **9,879×** |
| **Overall IPS** | 39,993,392 | 60,874 | **657×** |
| **Tests passed** | **51/51** | 51/51 | — |
| **Geometric mean speedup** | — | — | **16,929×** |
| **Min speedup** | — | — | 463× |
| **Max speedup** | — | — | 132,000× |

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

---

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

## Known Issues

1. **Instruction count mismatch** — Herve and Spike report different instruction counts for the same tests due to Spike's platform initialization overhead. This is expected behavior.
2. **Timer resolution** — Herve's execution times are extremely small (microseconds), approaching the resolution limits of `std::chrono::high_resolution_clock`. For more accurate timing on larger workloads, the benchmark suite should be extended with the riscv-tests benchmark programs (dhrystone, median, multiply, etc.) which execute millions of instructions.
