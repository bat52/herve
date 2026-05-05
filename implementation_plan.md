# Implementation Plan

[Overview]
Add the Dhrystone benchmark from riscv-tests to the Herve ISS vs Spike comparison pipeline.

The Dhrystone synthetic integer benchmark is a well-known CPU performance benchmark that exercises a representative mix of integer operations, control flow, and procedure calls. It is included in the riscv-tests benchmark suite at `riscv-tests/benchmarks/dhrystone/`. Currently, the Herve benchmark pipeline supports median, mm (matrix multiply), and memcpy benchmarks via the HTIF-based runner. This implementation adds Dhrystone as a new benchmark, following the same pattern: a Makefile build target for the `.riscv` ELF, Herve ISS runner support (via the existing HTIF runner), Spike runner support (via `--benchmark` flag), and a dedicated comparison target. Additionally, an `alloca()` implementation must be provided in `newlib-stubs/` since dhrystone uses `alloca()` for allocation and we compile with `-nostdlib`.

[Types]
No new types, interfaces, enums, or data structures are needed. The existing `HtifBenchmarkResult` struct in `rv32_dpi_benchmark_htif.cpp` and the CSV output format are reused.

[Files]
Three new files to create, two existing files to modify, one existing file to update.

New files:
- `dpi-riscv/newlib-stubs/alloca.c` — Implementation of `alloca()` for bare-metal RV32. Uses a simple bump pointer allocator that carves memory from the `_end` symbol. Aligns to 16 bytes.
- `dpi-riscv/herve_benchmark_dhrystone.csv` — Output file for Herve Dhrystone benchmark results.
- `dpi-riscv/spike_benchmark_dhrystone.csv` — Output file for Spike Dhrystone benchmark results.

Existing files to modify:
- `dpi-riscv/Makefile` — Add: (1) `alloca.o` compilation rule, (2) `dhrystone.riscv` build target, (3) `run_benchmark_dhrystone[_csv]` targets, (4) `run_spike_benchmark_dhrystone[_csv]` targets, (5) `compare_benchmark_dhrystone` target, (6) clean rules for new artifacts.
- `dpi-riscv/docs/benchmark_results.md` — Add a new section for Dhrystone benchmark results in the comparison table.

[Functions]
New function:
- `alloca(size_t size)` in `dpi-riscv/newlib-stubs/alloca.c`. Implements `void *alloca(size_t size)` using a static bump pointer initialized to `&_end` (the end of the BSS segment from the linker script). Returns 16-byte aligned memory. No free needed since dhrystone allocates twice during initialization only.

[Classes]
No class modifications needed.

[Dependencies]
No new external dependencies. The build requires:
- `riscv64-unknown-elf-gcc` (already used for `median.riscv`, `mm.riscv`, `memcpy.riscv`)
- `alloca.o` linked into `dhrystone.riscv`
- Standard riscv-tests benchmark infrastructure: `crt.S`, `syscalls.c`, `util.h`, `test.ld`, `encoding.h`

[Testing]
The test is the benchmark itself — run `make compare_benchmark_dhrystone` and verify:
1. Herve ISS runs `dhrystone.riscv` to completion via HTIF (exit code 0 = PASS).
2. Spike runs `dhrystone.riscv` and produces committed instructions.
3. The comparison script outputs a side-by-side table showing Herve vs Spike metrics.
4. The benchmark results can be viewed at `dpi-riscv/docs/benchmark_results.md`.

[Implementation Order]
Step-by-step implementation sequence:
1. Create `dpi-riscv/newlib-stubs/alloca.c` with a bump-pointer `alloca()` implementation.
2. Add `alloca.o` build rule to `dpi-riscv/Makefile` (compile with `$(RISCV_PREFIX)-gcc ... -c`).
3. Add `dhrystone.riscv` build target to the Makefile, depending on `alloca.o`.
4. Add Herve runner targets: `run_benchmark_dhrystone`, `run_benchmark_dhrystone_csv`.
5. Add Spike runner targets: `run_spike_benchmark_dhrystone`, `run_spike_benchmark_dhrystone_csv`.
6. Add comparison target: `compare_benchmark_dhrystone`.
7. Add new files to the `clean` target.
8. Update `benchmark_results.md` with Dhrystone section.
9. Test by building and running the comparison.
