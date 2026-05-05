# Implementation Plan

[Overview]
Transition the riscv-tests validation suite from using objcopy-converted .bin files to executing the native ELF files directly, ensuring Herve runs the identical ELF binaries that Spike executes in run_spike_benchmark.sh.

The existing workflow converts riscv-tests ELF binaries to flat .bin files via `riscv64-unknown-elf-objcopy -O binary`, then loads them at a fixed offset (0x80000000) into the ISS. Meanwhile, `run_spike_benchmark.sh` feeds the raw ELF files directly to Spike. This discrepancy means we're testing two different input formats. An in-memory ELF loader (`rv_load_elf`) has already been prototyped in the ISS core (`rv_init_elf` does file-based loading), but the standalone test runners (`rv32_dpi_riscv_tests.cpp` and `rv32_dpi_benchmark.cpp`) bypass it — they allocate a shared 2GB mmap buffer and load .bin files manually. The solution is to add a buffer-based ELF loader that writes into the pre-allocated RAM, then update the two test runners and the shell script to discover and load the same ELF files (bare files with no extension in `riscv-tests/isa/`) that Spike uses.

[Types]
No new types; the existing `struct elf32_ehdr` and `struct elf32_phdr` in `rv32_dpi.c` are reused.

[Files]

Single sentence describing file modifications.

Detailed breakdown:
- **`dpi-riscv/sim/iss/rv32_dpi.h`** — modified
  - Add declaration: `uint32_t rv_load_elf(const uint8_t *elf_data, size_t elf_size);`
  - This is a buffer-based ELF loader that works with a pre-allocated RAM buffer.

- **`dpi-riscv/sim/iss/rv32_dpi.c`** — modified
  - Implement `rv_load_elf(const uint8_t *elf_data, size_t elf_size)`:
    - Takes a pointer to an ELF file loaded into memory by the caller.
    - Parses the ELF header and program headers directly from the buffer.
    - Loads each `PT_LOAD` segment at its `p_vaddr` into the existing `memory[]` buffer.
    - Zero-fills BSS (`p_memsz > p_filesz`).
    - Returns the entry point (`e_entry`) on success, 0 on failure.
    - Does NOT allocate or free memory — assumes `memory` is already set up (via `rv_init()`, `rv_set_ram()`, or a prior `rv_init_elf()` call).
    - Does NOT call `fopen`/`fread` — operates purely in memory.
    - Validates: ELF magic, 32-bit, little-endian, RISC-V machine type.

- **`dpi-riscv/sim/iss/rv32_dpi_riscv_tests.cpp`** — modified
  - **File discovery**: change from scanning `*.bin` files to scanning for files matching the same patterns as `run_spike_benchmark.sh`:
    - Prefixes: `rv32ui-p-`, `rv32um-p-`, `rv32uc-p-`
    - Exclude directories, files ending in `.dump` or `.hex`
    - Filter by trying `fopen()` (same as current) — if the file opens and passes the prefix/extension filter, include it.
  - **Loading**: replace `load_file()` + `memcpy(&global_ram[TEST_BASE_ADDR], binary, binary_size)` with `load_file()` + `rv_load_elf(binary, binary_size)`, then `rv_reset(entry_point)`.
  - Remove the `TEST_BASE_ADDR` constant (no longer needed since ELF segments self-describe their addresses).
  - Change the default test directory from `../riscv-tests/isa/bin` to `../riscv-tests/isa`.
  - The `mmap`/`rv_set_ram` setup remains unchanged (the 2GB pre-allocated buffer).

- **`dpi-riscv/sim/iss/rv32_dpi_benchmark.cpp`** — modified
  - Identical changes to the file-discovery and loading logic as `rv32_dpi_riscv_tests.cpp`.
  - Change default directory from `../riscv-tests/isa/bin` to `../riscv-tests/isa`.
  - Replace manual binary loading with `rv_load_elf()` + `rv_reset(entry_point)`.
  - Remove `TEST_BASE_ADDR`.

- **`dpi-riscv/tests/run_riscv_tests.sh`** — modified
  - **Remove** the ELF → binary conversion loop (lines 70-79):
    ```bash
    for pattern in rv32ui-p- rv32um-p- rv32uc-p-; do
        for elf in "$RISCV_TESTS_DIR/isa/$pattern"*; do
            [ -f "$elf" ] || continue
            case "$elf" in *.dump|*.hex) continue ;; esac
            base="$(basename "$elf")"
            riscv64-unknown-elf-objcopy -O binary "$elf" "$TEST_BIN_DIR/$base.bin"
        done
    done
    ```
  - **Remove** the `TEST_BIN_DIR` variable and the `mkdir -p "$TEST_BIN_DIR"` line.
  - **Remove** the `.bin` file count check (`NUM_BINS`).
  - **Change** the test runner invocation from:
    ```bash
    ./rv32_dpi_riscv_tests "$TEST_BIN_DIR"
    ```
    to:
    ```bash
    ./rv32_dpi_riscv_tests "$RISCV_TESTS_DIR/isa"
    ```
  - The build step (`make XLEN=32 ...`) remains unchanged — it produces the bare ELF files in `riscv-tests/isa/`.

[Functions]

Single sentence describing function modifications.

Detailed breakdown:
- **New function**: `uint32_t rv_load_elf(const uint8_t *elf_data, size_t elf_size)` in `rv32_dpi.c`
  - Signature: `uint32_t rv_load_elf(const uint8_t *elf_data, size_t elf_size)`
  - File: `dpi-riscv/sim/iss/rv32_dpi.c`
  - Purpose: In-memory ELF loader that writes PT_LOAD segments into the existing ISS RAM buffer at their virtual addresses and returns the entry point.
  - Implementation: Reuses the ELF-parsing logic from `rv_init_elf()` but reads from a buffer instead of a file. No malloc/free.

- **Modified function**: `main()` in `rv32_dpi_riscv_tests.cpp`
  - Replace `.bin` extension filter with prefix-based filter matching `rv32ui-p-`, `rv32um-p-`, `rv32uc-p-`.
  - Use `rv_load_elf()` after loading file into memory, then `rv_reset(entry_point)`.
  - Remove `TEST_BASE_ADDR`.

- **Modified function**: `run_test()` in `rv32_dpi_riscv_tests.cpp`
  - Remove the `memcpy` at fixed offset. Instead, the caller loads ELF via `rv_load_elf()` and passes the entry point.
  - Keep the execution loop and gp-checking logic unchanged.

- **Modified function**: `main()` in `rv32_dpi_benchmark.cpp`
  - Same changes as `rv32_dpi_riscv_tests.cpp::main()`.

- **Modified function**: `run_test()` in `rv32_dpi_benchmark.cpp`
  - Same changes as `rv32_dpi_riscv_tests.cpp::run_test()`.

[Classes]

No class modifications — the codebase is C/C++ with no C++ classes beyond structs.

[Dependencies]

No new dependencies. The ELF structures (`struct elf32_ehdr`, `struct elf32_phdr`) and constants (`EM_RISCV`, `PT_LOAD`, etc.) are already defined inline in `rv32_dpi.c`.

[Testing]

The existing test framework (`test.sh` → `run_riscv_tests`) validates the change end-to-end. After modification, running `make run_riscv_tests` in `dpi-riscv/` must produce the same pass/fail results as before (all ~48 tests should pass). Running `make run_benchmark_csv` and `make run_spike_benchmark_csv` should produce CSV files referencing the same test names. Manual verification: `readelf -h` can confirm the ELFs Spike runs are the same files Herve will now load.

[Implementation Order]

Single sentence describing the implementation sequence.

Numbered steps showing the logical order:
1. **Add `rv_load_elf()` to `rv32_dpi.c`** — implement the buffer-based ELF loader using the inline ELF structures already present. This is the core enabling change.
2. **Update `rv32_dpi.h`** — add the `rv_load_elf` declaration.
3. **Update `rv32_dpi_riscv_tests.cpp`** — change file discovery from `.bin` extension to `rv32*ui/m/uc*-p-` prefixes; replace memcpy-based loading with `rv_load_elf()` + `rv_reset()`.
4. **Update `rv32_dpi_benchmark.cpp`** — apply the same changes as step 3.
5. **Update `run_riscv_tests.sh`** — remove the objcopy conversion loop, remove bin directory references, point the test runner to `riscv-tests/isa/` directly.
6. **Test** — run `make run_riscv_tests` from `dpi-riscv/` and verify all tests pass. Run `make run_benchmark` to confirm benchmark works with ELF files.
