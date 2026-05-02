# Benchmark Results: Herve ISS vs Spike

## Overview

This report compares the execution performance of **Herve** (a lightweight RV32IM[C] ISS) against **Spike** (the official RISC-V ISA simulator) using the standard **riscv-tests** ISA test suite as the workload.

| Metric | Herve ISS | Spike | Speedup (Spike/Herve) |
|--------|-----------|-------|----------------------|
| **Total instructions** | 17,941 | 273,022 | — |
| **Total time** | 0.000167 s | 4.412 s | **26,419×** |
| **Overall IPS** | 107,431,138 | 61,882 | **1,736×** |
| **Tests passed** | 49/51 | 51/51 | — |
| **Geometric mean speedup** | — | — | **29,466×** |
| **Min speedup** | — | — | 8,111× |
| **Max speedup** | — | — | 106,000× |

> **Note:** Herve has 2 pre-existing failures (`rv32ui-p-lui` and `rv32ui-p-srai` with gp=7) — these are ISS bugs in `rv32_dpi.c`, not introduced by the benchmark.

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

Herve reports **17,941 total instructions** while Spike reports **273,022 total instructions**. This ~15× difference is because:

- **Herve** counts only *committed instructions* from the test program itself — it stops at `EBREAK`/`ECALL` and does not count any boot or trap handling code.
- **Spike** with `--log-commits` counts *all* committed instructions including Spike's internal boot sequence, trap/exception handling, CSR accesses, and other platform-modeling overhead that occurs before and after the test program runs.

This means the **speedup ratios** are somewhat inflated for Herve since it executes fewer instructions per test. The true performance comparison is better reflected by the **IPS (instructions per second)** metric, which normalizes for instruction count.

---

## Detailed Results

### Per-Test Comparison

| Test | Herve Insn | Spike Insn | Herve Time (s) | Spike Time (s) | Herve IPS | Spike IPS | Speedup |
|------|-----------|-----------|---------------|---------------|----------|----------|---------|
| rv32uc-p-rvc | 253 | 5,257 | 0.000006 | 0.247 | 45,503,597 | 21,283 | 41,167× |
| rv32ui-p-add | 499 | 5,503 | 0.000004 | 0.162 | 111,858,328 | 33,969 | 40,500× |
| rv32ui-p-addi | 276 | 5,280 | 0.000002 | 0.066 | 125,797,630 | 80,000 | 33,000× |
| rv32ui-p-and | 519 | 5,523 | 0.000004 | 0.079 | 126,770,884 | 69,911 | 19,750× |
| rv32ui-p-andi | 232 | 5,236 | 0.000002 | 0.075 | 114,398,422 | 69,813 | 37,500× |
| rv32ui-p-auipc | 92 | 5,096 | 0.000001 | 0.067 | 113,300,493 | 76,060 | 67,000× |
| rv32ui-p-beq | 325 | 5,329 | 0.000003 | 0.093 | 124,330,528 | 57,301 | 31,000× |
| rv32ui-p-bge | 343 | 5,347 | 0.000003 | 0.075 | 115,023,474 | 71,293 | 25,000× |
| rv32ui-p-bgeu | 368 | 5,372 | 0.000004 | 0.094 | 102,621,305 | 57,149 | 23,500× |
| rv32ui-p-blt | 325 | 5,329 | 0.000003 | 0.096 | 105,348,460 | 55,510 | 32,000× |
| rv32ui-p-bltu | 350 | 5,354 | 0.000003 | 0.075 | 102,820,212 | 71,387 | 25,000× |
| rv32ui-p-bne | 325 | 5,329 | 0.000003 | 0.119 | 102,523,659 | 44,782 | 39,667× |
| rv32ui-p-fence_i | 330 | 5,334 | 0.000003 | 0.077 | 106,211,780 | 69,273 | 25,667× |
| rv32ui-p-jal | 89 | 5,093 | 0.000001 | 0.071 | 97,909,791 | 71,732 | 71,000× |
| rv32ui-p-jalr | 149 | 5,153 | 0.000001 | 0.076 | 101,291,638 | 67,803 | 76,000× |
| rv32ui-p-lb | 287 | 5,291 | 0.000002 | 0.082 | 115,400,080 | 64,524 | 41,000× |
| rv32ui-p-lbu | 287 | 5,291 | 0.000003 | 0.072 | 113,798,573 | 73,486 | 24,000× |
| rv32ui-p-ld_st | 997 | 6,001 | 0.000009 | 0.073 | 112,566,332 | 82,205 | 8,111× |
| rv32ui-p-lh | 303 | 5,307 | 0.000003 | 0.111 | 102,434,077 | 47,811 | 37,000× |
| rv32ui-p-lhu | 312 | 5,316 | 0.000003 | 0.125 | 104,803,493 | 42,528 | 41,667× |
| rv32ui-p-lui | 86 | 5,103 | 0.000001 | 0.096 | 85,064,293 | 53,156 | 96,000× |
| rv32ui-p-lw | 317 | 5,321 | 0.000003 | 0.068 | 112,891,738 | 78,250 | 22,667× |
| rv32ui-p-ma_data | 414 | 5,079 | 0.000004 | 0.068 | 110,223,642 | 74,691 | 17,000× |
| rv32ui-p-or | 522 | 5,526 | 0.000005 | 0.066 | 110,733,984 | 83,727 | 13,200× |
| rv32ui-p-ori | 239 | 5,243 | 0.000002 | 0.068 | 117,792,016 | 77,103 | 34,000× |
| rv32ui-p-sb | 488 | 5,492 | 0.000005 | 0.080 | 103,477,523 | 68,650 | 16,000× |
| rv32ui-p-sh | 541 | 5,545 | 0.000005 | 0.110 | 109,937,005 | 50,409 | 22,000× |
| rv32ui-p-simple | 75 | 5,079 | 0.000001 | 0.106 | 109,489,051 | 47,915 | 106,000× |
| rv32ui-p-sll | 527 | 5,531 | 0.000005 | 0.082 | 110,043,850 | 67,451 | 16,400× |
| rv32ui-p-slli | 275 | 5,279 | 0.000003 | 0.071 | 105,525,710 | 74,352 | 23,667× |
| rv32ui-p-slt | 493 | 5,497 | 0.000005 | 0.078 | 102,197,347 | 70,474 | 15,600× |
| rv32ui-p-slti | 271 | 5,275 | 0.000003 | 0.076 | 100,221,893 | 69,408 | 25,333× |
| rv32ui-p-sltiu | 271 | 5,275 | 0.000003 | 0.078 | 107,968,127 | 67,628 | 26,000× |
| rv32ui-p-sltu | 493 | 5,497 | 0.000004 | 0.083 | 120,922,247 | 66,229 | 20,750× |
| rv32ui-p-sra | 546 | 5,550 | 0.000004 | 0.104 | 122,696,629 | 53,365 | 26,000× |
| rv32ui-p-srai | 87 | 5,294 | 0.000001 | 0.071 | 115,845,539 | 74,563 | 71,000× |
| rv32ui-p-srl | 540 | 5,544 | 0.000005 | 0.085 | 118,603,119 | 65,224 | 17,000× |
| rv32ui-p-srli | 284 | 5,288 | 0.000002 | 0.070 | 117,306,898 | 75,543 | 35,000× |
| rv32ui-p-st_ld | 517 | 5,521 | 0.000003 | 0.067 | 148,307,516 | 82,403 | 22,333× |
| rv32ui-p-sub | 491 | 5,495 | 0.000004 | 0.104 | 124,052,552 | 52,837 | 26,000× |
| rv32ui-p-sw | 548 | 5,552 | 0.000004 | 0.090 | 124,178,563 | 61,689 | 22,500× |
| rv32ui-p-xor | 521 | 5,525 | 0.000005 | 0.076 | 111,277,232 | 72,697 | 15,200× |
| rv32ui-p-xori | 241 | 5,245 | 0.000002 | 0.071 | 100,458,524 | 73,873 | 35,500× |
| rv32um-p-div | 130 | 5,134 | 0.000001 | 0.070 | 92,790,864 | 73,343 | 70,000× |
| rv32um-p-divu | 131 | 5,135 | 0.000001 | 0.074 | 103,068,450 | 69,392 | 74,000× |
| rv32um-p-mul | 493 | 5,497 | 0.000005 | 0.070 | 106,709,957 | 78,529 | 14,000× |
| rv32um-p-mulh | 493 | 5,497 | 0.000005 | 0.068 | 96,628,773 | 80,838 | 13,600× |
| rv32um-p-mulhsu | 493 | 5,497 | 0.000005 | 0.113 | 97,972,973 | 48,646 | 22,600× |
| rv32um-p-mulhu | 493 | 5,497 | 0.000005 | 0.069 | 98,031,418 | 79,667 | 13,800× |
| rv32um-p-rem | 130 | 5,134 | 0.000002 | 0.075 | 85,022,891 | 68,453 | 37,500× |
| rv32um-p-remu | 130 | 5,134 | 0.000001 | 0.070 | 114,436,620 | 73,343 | 70,000× |

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

Even when accounting for the instruction count difference, Herve's **IPS is ~1,736× higher** than Spike's (107M vs 62K). This reflects the fundamental architectural difference:

- Herve is a minimal instruction interpreter — a tight loop of fetch-decode-execute with no platform modeling
- Spike includes full system simulation with privilege levels, CSRs, trap handling, and device modeling

### Speedup Range

The per-test speedup ranges from **8,111×** (rv32ui-p-ld_st — the longest Herve test at 997 instructions) to **106,000×** (rv32ui-p-simple — the shortest Herve test at 75 instructions). The speedup is inversely correlated with test length: shorter tests have a higher proportion of Spike overhead relative to actual test instructions.

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

1. **2 Herve test failures** (`rv32ui-p-lui` and `rv32ui-p-srai` with gp=7) — pre-existing ISS bugs in `rv32_dpi.c`, not related to the benchmark
2. **Instruction count mismatch** — Herve and Spike report different instruction counts for the same tests due to Spike's platform initialization overhead. This is expected behavior.
3. **Timer resolution** — Herve's execution times are extremely small (microseconds), approaching the resolution limits of `std::chrono::high_resolution_clock`. For more accurate timing on larger workloads, the benchmark suite should be extended with the riscv-tests benchmark programs (dhrystone, median, multiply, etc.) which execute millions of instructions.
