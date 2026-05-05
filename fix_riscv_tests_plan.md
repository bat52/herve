# Implementation Plan: Fix `run_riscv_tests` Failure on Remote CI

## Overview

Fix the `run_riscv_tests` make target so it passes on the remote CI machine (GitHub Actions) by building the riscv-tests ISA ELF binaries as a prerequisite when the toolchain is available, and gracefully skipping when they cannot be built.

The root cause is that `rv32_dpi_riscv_tests.cpp` scans `../riscv-tests/isa/` for RV32 ELF binaries matching `rv32ui-p-*`, `rv32um-p-*`, `rv32uc-p-*`. These ELF binaries are build artifacts (gitignored via `rv*-*` in `.gitignore`) and are never built during CI. The test runner finds 0 matching files, runs 0 tests, and `test.sh` exits with code 2 ("No tests were able to run").

The fix has three parts:
1. **Build riscv-tests ELFs** as a prerequisite in the Makefile when the RISC-V toolchain is available
2. **Graceful skip** in `test.sh` when ELFs can't be built (toolchain missing or build failure)
3. **Clear exit code** from `rv32_dpi_riscv_tests.cpp` to distinguish "0 tests ran" from "tests ran and passed"

[Types]
No new types are needed. The existing `TestResult` struct in `rv32_dpi_riscv_tests.cpp` is sufficient.

[Files]
Three files modified, no new files.

### Modified Files

- **`dpi-riscv/Makefile`** — Add `build_riscv_tests` target and wire it as a prerequisite for `run_riscv_tests`:
  - New target `build_riscv_tests`: Runs `make -C ../riscv-tests/isa XLEN=32` to build RV32 test ELF binaries
  - Modify `run_riscv_tests` target: Add `build_riscv_tests` as a prerequisite (before the binary build)
  - Add `build_riscv_tests` to `.PHONY`

- **`dpi-riscv/tests/test.sh`** — Add skip logic for `run_riscv_tests`:
  - After tool detection, check if riscv-tests ISA ELF binaries exist (at least one `rv32ui-p-*` file in `../riscv-tests/isa/`)
  - If no ELF binaries exist AND the RISC-V toolchain is available, attempt to build them via `make build_riscv_tests`
  - If the build fails or toolchain is unavailable, set a skip reason for `run_riscv_tests`

- **`dpi-riscv/sim/iss/rv32_dpi_riscv_tests.cpp`** — Improve exit code handling:
  - When 0 matching test files are found, print a clear warning and exit with code 0 (not 1)
  - This prevents `test.sh` from seeing a failure when no tests could run

[Functions]
No new functions. Minor modifications to existing functions.

### Modified Functions

- **`dpi-riscv/sim/iss/rv32_dpi_riscv_tests.cpp :: main()`** — Change exit behavior when 0 tests found:
  - Currently: `return (failed > 0) ? 1 : 0;` — returns 0 even when 0 tests ran
  - Change: Add explicit handling for `num_tests == 0` case — print "No matching test ELF files found" and return 0
  - The real fix is in `test.sh` which checks `$PASS -eq 0` and exits with code 2

- **`dpi-riscv/tests/test.sh`** — Add riscv-tests build step and skip logic:
  - After tool detection section, add a check for riscv-tests ELF presence
  - If `HAS_TOOLCHAIN=1` and no ELFs found, run `make build_riscv_tests` and check result
  - If still no ELFs after attempted build, set `SKIP_RISCV_TESTS` reason for `run_riscv_tests`

[Classes]
No classes are modified.

[Dependencies]
No new external dependencies. The build step depends on the RISC-V toolchain (`riscv64-unknown-elf-gcc`), which is already installed by `install_toolchain.sh` and detected by `test.sh`.

[Testing]
### Manual Verification
1. Run `make build_riscv_tests` from `dpi-riscv/` — should compile RV32 test ELFs in `../riscv-tests/isa/`
2. Run `make run_riscv_tests` — should run all 51 tests and pass
3. Run `bash tests/test.sh` — should show `run_riscv_tests` as PASS
4. Simulate CI: remove ELF binaries, run `bash tests/test.sh` — should build them and pass

### Regression
- `make run_standalone` should still pass
- `make run_c_test` should still pass
- `make run_irq_standalone` should still pass
- All Verilator-based tests should still pass

[Implementation Order]
The implementation must follow this dependency order:

1. **Modify `rv32_dpi_riscv_tests.cpp`** — Add clear message when 0 tests found, return 0
2. **Modify `dpi-riscv/Makefile`** — Add `build_riscv_tests` target, wire as prerequisite for `run_riscv_tests`
3. **Modify `dpi-riscv/tests/test.sh`** — Add riscv-tests build step and skip logic
4. **Test locally** — Verify the full pipeline works end-to-end
