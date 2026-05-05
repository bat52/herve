# Implementation Plan: Fix Failing ModelSim DPI-C Tests

## Overview

Fix issues causing all 5 ModelSim DPI-C tests to fail in `run_modelsim.sh`.

The error log shows three root causes:
1. **`firmware_mmio_regs` Makefile target** — not in `.PHONY`, causing Make to invoke the host linker on RISC-V object files
2. **Blank line in `firmware_ahb.o` Makefile rule** — disconnects the recipe from its target
3. **ModelSim tool invocation** — `vlib`/`vlog`/`vsim` are invoked via absolute paths to `bin/` symlinks, but the `vco` dispatcher script resolves the platform directory relative to the symlink target's resolved location, which fails because `bin/` is a symlink to `../vco` and the platform binaries live in `../linux/` not `bin/linux/`

Additionally, a **system-level dependency** was discovered: ModelSim ASE 20.1 is a 32-bit application and requires 32-bit compatibility libraries (`libc6:i386`, `lib32stdc++6`) which are not installed on this 64-bit-only system.

## Types

No new types. All changes are to shell script logic and Makefile rules.

## Files

### Existing Files to Modify

| File | Change |
|---|---|
| `dpi-riscv/Makefile` | Add `firmware_mmio_regs` to `.PHONY` list; remove blank line in `firmware_ahb.o` rule |
| `dpi-riscv/tests/run_modelsim.sh` | Fix ModelSim tool invocation: add `MODELSIM_DIR/bin` to `PATH` and invoke tools by basename instead of absolute path; remove `trap RETURN` cleanup; add runtime verification that ModelSim tools are actually executable |

## Functions

### `dpi-riscv/Makefile`

**Line 46 — `.PHONY` list:**
Add `firmware_mmio_regs` to the `.PHONY` declaration so Make never tries to link it as a host executable.

**Lines 180-182 — `firmware_ahb.o` rule:**
Remove the blank line between the target line and the recipe line so the recipe is correctly associated with the target.

### `dpi-riscv/tests/run_modelsim.sh`

**ModelSim tool detection (lines 130-195):**
Instead of setting `VLIB="$MODELSIM_DIR/vlib"`, `VLOG="$MODELSIM_DIR/vlog"`, `VSIM="$MODELSIM_DIR/vsim"` (which are symlinks to `../vco`), change the approach:
- When ModelSim is found via `MODELSIM_DIR` or `INTELFPGA_DIR`, add `$MODELSIM_DIR/bin` to `PATH`
- Set `VLIB="vlib"`, `VLOG="vlog"`, `VSIM="vsim"` (basenames only)
- This allows the `vco` dispatcher to resolve `$0` correctly and find the platform-specific binary

**Runtime verification (after detection):**
Add a check that runs `vlib` to verify it actually executes. If it fails (e.g. due to missing 32-bit libraries), print a helpful warning with installation instructions and mark ModelSim as unavailable so tests are skipped gracefully instead of failing.

**`run_test` function — trap cleanup (line 56):**
Remove `trap 'rm -rf "$work_dir"' RETURN` since only one RETURN trap can be active at a time in bash, causing issues with multiple test invocations.

## Classes

No class modifications.

## Dependencies

No new dependencies. However, to actually run ModelSim ASE 20.1 on a 64-bit system, the following 32-bit compatibility libraries must be installed:
```
sudo bash tests/install_modelsim_deps.sh
```

A helper script `tests/install_modelsim_deps.sh` is provided that automates this process.

## Testing

Run `bash tests/run_modelsim.sh` and verify:
1. All firmware builds succeed (no more host-linker errors)
2. ModelSim detection correctly identifies when tools are present but non-functional
3. Tests are gracefully skipped with a clear warning when 32-bit libraries are missing
4. After installing 32-bit libraries, all 5 tests produce `[PASS]` results

## Implementation Order

1. Fix `dpi-riscv/Makefile` — add `firmware_mmio_regs` to `.PHONY`, fix blank line in `firmware_ahb.o` rule
2. Fix `dpi-riscv/tests/run_modelsim.sh` — change ModelSim tool invocation to use PATH-based lookup, remove trap cleanup, add runtime verification
3. Run `bash tests/run_modelsim.sh` to verify graceful handling of missing 32-bit libraries
