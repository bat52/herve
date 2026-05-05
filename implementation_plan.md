# Implementation Plan

Add the `median` benchmark from `riscv-tests/benchmarks/` as an HTIF-based workload in the Herve vs Spike benchmark comparison pipeline.

The median benchmark uses a fundamentally different completion mechanism than the existing ISA tests: instead of setting `gp=1` and executing `EBREAK`, it writes to an HTIF `tohost` memory-mapped variable at address `0x80001000`. This requires a new benchmark runner that can detect HTIF exit signaling, plus a Makefile target to build the `median.riscv` ELF binary from source. The runner will integrate into the existing CSV-based comparison pipeline (`analyze_benchmark.py`) and will be accessible through new and updated Makefile targets.

## Context and Background

The existing benchmark infrastructure (`rv32_dpi_benchmark.cpp`, `run_spike_benchmark.sh`, `analyze_benchmark.py`) supports only ISA tests (rv32ui-p-\*, rv32um-p-\*, rv32uc-p-\*) that use `gp=1` + `EBREAK` for pass/fail signaling and require no runtime bootstrap.

The riscv-tests benchmark suite (median, qsort, dhrystone, etc.) differs fundamentally:
- **Completion**: Uses HTIF `tohost`/`fromhost` protocol — writes `(exit_code << 1) | 1` to address `0x80001000`
- **Runtime**: Requires `_init` → `thread_entry` → `main()` bootstrap via `crt.S`, plus `syscalls.c` for HTIF syscalls and `printf` support
- **CSR access**: Uses `mcycle`/`minstret` CSRs for `setStats()` performance counting
- **Build**: Must be compiled from C source using the RISC-V GCC toolchain; no pre-built binaries exist

The plan adds:
1. A Makefile target to build `median.riscv` from the riscv-tests sources
2. A new HTIF-aware benchmark runner `rv32_dpi_benchmark_htif.cpp`
3. Extensions to `run_spike_benchmark.sh` to run benchmark ELFs on Spike
4. New Makefile targets to build, run, and compare the median benchmark
5. Documentation updates

[Types]
One new struct type in the HTIF benchmark runner to store per-benchmark results.

```
// in sim/iss/rv32_dpi_benchmark_htif.cpp
struct HtifBenchmarkResult {
    std::string name;
    bool passed;
    int instructions;
    double time_sec;
    double ips;
    std::string reason;
    int exit_code;       // exit code extracted from tohost (0 = pass)
};
```

No new types in the ISS core (`rv32_dpi.c` or `rv32_dpi.h`) — HTIF detection is done entirely in the benchmark runner by inspecting the RAM buffer after execution.

[Files]
Two new files, four modified files.

### New Files

- **`dpi-riscv/sim/iss/rv32_dpi_benchmark_htif.cpp`** — HTIF-based benchmark runner.
  - Discovers `.riscv` ELF files in a configurable benchmark directory (default: the project root, matching `median.riscv`)
  - For each benchmark:
    1. Loads the ELF via `rv_load_elf()` into pre-allocated RAM
    2. Resets PC to ELF entry point
    3. Runs `rv_step(1000)` in a loop, measuring wall-clock time
    4. After each batch, reads 8 bytes at RAM offset `0x80001000` (the `tohost` address from `test.ld`)
    5. If `tohost & 1 == 1`, the benchmark has exited; extracts `exit_code = tohost >> 1`
    6. If `exit_code == 0`, benchmark passes; otherwise fails with the exit code
    7. Also detects stale PC (infinite loop) and instruction limit as timeout
  - Outputs human-readable and CSV results matching the same format as `rv32_dpi_benchmark.cpp`
  - CSV columns: `test_name,passed,instructions,time_sec,ips,reason`

### Modified Files

- **`dpi-riscv/Makefile`** — Add build and run targets:
  - **build target `median.riscv`**: Compiles the median benchmark using the RISC-V toolchain, linking with `riscv-tests/benchmarks/common/crt.S`, `syscalls.c`, `util.h`, and `riscv-tests/env/encoding.h`. Uses `-march=rv32im_zicsr -mabi=ilp32` for RV32 compatibility with CSR access.
  - **build target `rv32_dpi_benchmark_htif`**: Compiles the HTIF runner from `sim/iss/rv32_dpi_benchmark_htif.cpp` and `sim/iss/rv32_dpi.c`.
  - **target `run_benchmark_htif`**: Builds and runs the HTIF runner — runs median.riscv on Herve.
  - **target `run_spike_benchmark_median`**: Runs the median benchmark binary on Spike via `run_spike_benchmark.sh --benchmark median.riscv`.
  - **target `compare_benchmark_htif`**: Compares Herve and Spike CSV outputs using `analyze_benchmark.py`.
  - **clean target** update: Add `median.riscv median.riscv.dump herve_benchmark_htif.csv spike_benchmark_htif.csv rv32_dpi_benchmark_htif`.

- **`dpi-riscv/tests/run_spike_benchmark.sh`** — Add benchmark mode:
  - Accept `--benchmark <path-to-elf>` flag to run a single benchmark ELF on Spike
  - Use HTIF exit protocol to determine pass/fail:
    - Spike should crash or loop on HTIF exit, but we can check tohost via the log or instruction count
    - Alternative: check that Spike ran without crashing (similar to existing pattern but less strict since benchmarks are longer-running)
  - Output CSV in the same format as ISA test runs (`test_name,passed,instructions,time_sec,ips,reason`)
  - Separate ISPC (instructions per cycle) measurement not needed — use `--log-commits` as before

- **`dpi-riscv/docs/benchmark_results.md`** — Add median benchmark section documenting the HTIF methodology and results.

- **`dpi-riscv/docs/isa_support.md`** — Add note about benchmark support (HTIF tohost detection) if this file documents such capabilities.

### Files NOT modified

- `rv32_dpi.c` / `rv32_dpi.h` — No changes to the ISS core. HTIF detection is done externally in the runner.
- `rv32_dpi_benchmark.cpp` — Left untouched. The existing ISA test runner continues to work as before.
- `analyze_benchmark.py` — No changes needed. It already compares CSV files generically, matching test names between two CSV inputs.

[Functions]
One new function definition in the HTIF benchmark runner.

### New Functions

- **`dpi-riscv/sim/iss/rv32_dpi_benchmark_htif.cpp :: main()`** — Standard entry point.
  - Parses `--csv` flag and optional benchmark directory path
  - Discovers `.riscv` benchmark ELF files in the directory
  - For each, loads ELF, runs measurement loop, checks HTIF tohost
  - Outputs results

- **`dpi-riscv/sim/iss/rv32_dpi_benchmark_htif.cpp :: run_htif_benchmark()`** (helper) — Runs a single HTIF benchmark:
  - Signature: `static int run_htif_benchmark(const uint8_t *elf_data, size_t elf_size, const char *test_name, HtifBenchmarkResult *result)`
  - Loads ELF, resets ISS, runs rv_step batches, measures time, checks tohost at offset 0x80001000
  - Returns 0 on clean exit (HTIF detected), -1 on timeout/error

- **`dpi-riscv/sim/iss/rv32_dpi_benchmark_htif.cpp :: load_file()`** (helper) — Identical to the version in `rv32_dpi_benchmark.cpp`: loads a file into a malloc'd buffer.

### Modified Functions

None. The ISS core is unchanged.

[Classes]
No classes are modified. Both the existing codebase and the new file use C and C++ (with free functions, not class methods).

[Dependencies]
No new external dependencies. The HTIF benchmark runner uses only standard C/C++ headers already in use:
- `rv32_dpi.h` (ISS API — already exists)
- Standard headers: `<stdint.h>`, `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<dirent.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<unistd.h>`, `<chrono>`, `<vector>`, `<string>`, `<algorithm>`, `<cmath>`

The `median.riscv` build target depends on the RISC-V GCC toolchain (`riscv64-unknown-elf-gcc` with multilib support for RV32), which must already be installed (it's a prerequisite for building firmware).

[Testing]
The new benchmark runner and build target must be verified individually and as part of the pipeline.

### Manual Verification Steps

1. **Build the median benchmark**: `make median.riscv` — should produce `median.riscv` ELF binary
2. **Run on Herve**: `make run_benchmark_htif` — should execute the median benchmark and output results
3. **Run on Spike**: `make run_spike_benchmark_median` — should run median.riscv on Spike and output CSV
4. **Compare**: `make compare_benchmark_htif` — should show side-by-side comparison of Herve vs Spike

### Pass Criteria

- Herve ISS successfully runs the median benchmark to completion (detects HTIF tohost write)
- Herve reports PASS for the median benchmark (exit code 0 → verify_data matches)
- Spike also runs and reports PASS
- The comparison script shows both results

### Regression Tests

- `make run_standalone` should still pass (existing standalone test)
- `make run_riscv_tests` should still pass (existing ISA test runner)
- `make run_benchmark` should still work (existing ISA benchmark runner)
- `bash tests/test.sh` should pass all applicable tests

[Implementation Order]
The implementation must follow this dependency order:

1. **Build `median.riscv` binary** — Add Makefile target to compile the median benchmark from riscv-tests sources. Verify the binary exists and can be run on Spike as a sanity check.

2. **Create `rv32_dpi_benchmark_htif.cpp`** — Implement the HTIF-aware benchmark runner. Build and test against `median.riscv` on Herve ISS.

3. **Update `run_spike_benchmark.sh`** — Add `--benchmark` flag support for running a single benchmark ELF on Spike.

4. **Update `Makefile`** — Add all new targets (`run_benchmark_htif`, `run_spike_benchmark_median`, `compare_benchmark_htif`, clean entries).

5. **Update documentation** — Update `benchmark_results.md` and `isa_support.md` with median benchmark methodology and results.

6. **Run full comparison** — Execute Herve vs Spike median benchmark comparison and verify the pipeline end-to-end.
