# Implementation Plan: Add mm Benchmark to Herve vs Spike Pipeline

Add the `mm` (matrix multiply) benchmark from `riscv-tests/benchmarks/mm/` to the existing Herve vs Spike benchmark comparison pipeline, following the same pattern as the previously integrated `median` benchmark.

The `mm` benchmark uses HTIF for completion signaling (same as `median`) but introduces a new challenge: it uses `double` arithmetic and the `fma()` (fused multiply-add) function. Since Herve ISS supports only RV32IMC (no floating-point), the benchmark must be compiled with soft-float emulation via `-lm -lgcc`.

[Types]
No new types required. The existing `HtifBenchmarkResult` struct in `rv32_dpi_benchmark_htif.cpp` is generic and works for any `.riscv` ELF.

[Files]
One new file will be created (the implementation plan itself). Four existing files will be modified.

### New Files

- **`dpi-riscv/tests/implementation_plan_mm.md`** — This document. Implementation plan for the mm benchmark addition.

### Modified Files

- **`dpi-riscv/Makefile`** — Add mm benchmark build and run targets:
  - **build target `mm.riscv`**: Compile the mm benchmark from `riscv-tests/benchmarks/mm/` sources using `-march=rv32im_zicsr -mabi=ilp32` with `-lm -lgcc` for soft-float `double`/`fma()` support. Same compiler flags as `median.riscv` plus `-lm`.
  - **target `run_spike_benchmark_mm`**: Run mm.riscv on Spike.
  - **target `run_spike_benchmark_mm_csv`**: Run mm.riscv on Spike (CSV output).
  - **target `compare_benchmark_mm`**: Compare Herve vs Spike CSV outputs for mm.
  - No new runner needed — `rv32_dpi_benchmark_htif` auto-discovers all `.riscv` files.
  - **clean target update**: Add `mm.riscv mm.riscv.dump`.

- **`dpi-riscv/docs/benchmark_results.md`** — Add mm benchmark documentation and results (once generated).

### Files NOT modified

- `rv32_dpi.c` / `rv32_dpi.h` — No changes to the ISS core.
- `rv32_dpi_benchmark_htif.cpp` — Already auto-discovers `.riscv` ELFs; no changes needed.
- `run_spike_benchmark.sh` — Already supports `--benchmark <elf>`; no changes needed.
- `analyze_benchmark.py` — Already compares CSV files generically; no changes needed.

[Functions]
No new functions required. The existing `run_htif_benchmark()` and `main()` in `rv32_dpi_benchmark_htif.cpp` automatically handle any `.riscv` ELF.

[Classes]
No classes are modified.

[Dependencies]

### Soft-float Library Dependencies

The `mm` benchmark introduces a dependency on soft-float runtime libraries:

- **`-lm`** — Provides `fma()`, `fabs()`, and other math functions for `double` arithmetic. The mm benchmark uses `fma()` extensively in the inner loop kernel (`rb.h` has hundreds of `fma()` calls) and `fabs()` in the verification loop (`mm_main.c` line 61).
- **`-lgcc`** — Provides soft-float `double` arithmetic operations (add, sub, mul, div, comparisons) since RV32 has no FPU. GCC automatically inserts calls to `__adddf3`, `__muldf3`, `__subdf3`, `__divdf3`, `__eqdf2` etc. when compiling `double` operations for RV32.

### Build Dependencies

The `mm.riscv` build target depends on the same set of source files as `median.riscv`:

- `riscv-tests/benchmarks/mm/mm_main.c` — Main test harness with `thread_entry()`
- `riscv-tests/benchmarks/mm/mm.c` — Matrix multiply implementation
- `riscv-tests/benchmarks/mm/common.h` — Defines `t` as `double` (or `float` with `-DSP`), declares `mm()`, includes `rb.h`
- `riscv-tests/benchmarks/mm/rb.h` — Register-blocked inner loop kernel (auto-generated, heavily uses `fma()`)
- `riscv-tests/benchmarks/common/crt.S` — Startup code (`_init` → `thread_entry`)
- `riscv-tests/benchmarks/common/syscalls.c` — HTIF syscall implementations (tohost at `0x80001000`)
- `riscv-tests/benchmarks/common/util.h` — Utility macros (`exit()`, `barrier()`, `lfsr()`, `read_csr()`)
- `riscv-tests/benchmarks/common/test.ld` — Linker script defining memory layout and `tohost` at `0x80001000`
- `riscv-tests/env/encoding.h` — CSR register field definitions (for `read_csr(minstret)`, `read_csr(mcycle)`)

[Testing]
The new build target and pipeline must be verified individually and as part of the full comparison.

### Build Verification

1. `make mm.riscv` — Should produce `mm.riscv` ELF binary (expect soft-float library calls linked in)
2. `file mm.riscv` — Should confirm ELF 32-bit LSB RISC-V executable
3. `riscv64-unknown-elf-objdump -d mm.riscv | grep -c fma` — Should show `fma` calls (from libm) resolved in the binary
4. `riscv64-unknown-elf-objdump -d mm.riscv | grep -c "__adddf3\|__muldf3\|__subdf3\|__divdf3"` — Should show soft-float library calls

### Pipeline Verification

1. **Run on Herve**: `./rv32_dpi_benchmark_htif` — Should auto-discover `mm.riscv` and `median.riscv`, run both, report results
2. **Run on Spike**: `make run_spike_benchmark_mm` — Should run mm.riscv on Spike and output CSV
3. **Compare**: `make compare_benchmark_mm` — Should compare Herve vs Spike mm results

### Pass Criteria

- Herve ISS runs mm.riscv to completion (detects HTIF tohost write with exit code 0)
- Herve reports PASS for mm.riscv
- Spike also runs and reports PASS
- The comparison script shows both results side-by-side

### Expected Performance

Based on the block sizes in `rb.h` (`CBM=24, CBN=25, CBK=24`) and R=8 repetitions, the mm benchmark computes:
- Matrix dimensions: m=24, n=25, p=24
- FLOPs per mm call: 2 × 24 × 25 × 24 = 28,800
- Total FLOPs: 28,800 × 8 = 230,400
- Plus verification loop: 24 × 25 × 24 = 14,400 operations

Estimated instruction count on Herve: ~200K-500K instructions (due to soft-float library overhead making each `double` op many instructions). This is significantly larger than `median.riscv` (~11K instructions on Herve), making it a more meaningful benchmark.

### Regression Tests

- `make run_standalone` — Should still pass
- `make run_c_test` — Should still pass
- `make run_riscv_tests` — Should still pass
- `make run_benchmark_htif` — Should still pass (now runs both median.riscv and mm.riscv)

[Implementation Order]
The implementation follows a straightforward dependency order:

1. **Add `mm.riscv` build target to Makefile** — Copy the `median.riscv` pattern, changing source files to `mm/mm_main.c` `mm/mm.c` and adding `-lm` to the link flags. Verify with `make mm.riscv`.
2. **Add Spike run targets** — Add `run_spike_benchmark_mm` and `run_spike_benchmark_mm_csv` targets following the `run_spike_benchmark_median` pattern.
3. **Add comparison target** — Add `compare_benchmark_mm` target following the `compare_benchmark_htif` pattern.
4. **Update clean target** — Add `mm.riscv mm.riscv.dump` to the clean list.
5. **Run full comparison** — Execute `make compare_benchmark_mm` and record results.
6. **Update `benchmark_results.md`** — Add mm benchmark section with results and updated Makefile target table.
7. **Run full test suite** — `make clean && bash tests/test.sh` to verify nothing is broken.
