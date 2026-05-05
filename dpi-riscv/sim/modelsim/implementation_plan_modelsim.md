# Implementation Plan: Add INTELFPGA_DIR Variable to ModelSim Scripts

## Overview

Add an `INTELFPGA_DIR` environment variable to both ModelSim-related shell scripts
so the Intel FPGA Quartus/ModelSim installation folder is configurable instead of
hardcoded.

The user has ModelSim Intel FPGA Starter Edition installed at
`~/intelFPGA/20.1/modelsim_ase/`. The tools (`vsim`, `vlib`, `vlog`) are accessed
via symlinks in `~/intelFPGA/20.1/modelsim_ase/bin/`, each pointing to the `../vco`
platform-detection script.

Currently, `run_modelsim.sh` hardcodes candidate paths like
`/opt/intelFPGA/20.1/modelsim_ase/linux_x86_64` — none of which match the user's
actual installation layout (`linuxaloem` platform), and `install_modelsim.sh`
defaults to `/opt/intelFPGA/20.1/modelsim_ase` (system-wide) rather than
`$HOME/intelFPGA` (user-local).

The fix introduces `INTELFPGA_DIR` (default: `$HOME/intelFPGA`) and dynamically
discovers the latest version and the `bin/` subdirectory, making both scripts
agnostic to the platform-specific binary directory name (`linuxaloem`, `linux_x86_64`, etc.).

## Types

No new types. All changes are to shell script logic and defaults.

## Files

### Existing Files to Modify

| File | Change |
|---|---|
| `dpi-riscv/tests/run_modelsim.sh` | Replace hardcoded `MODELSIM_CANDIDATES` array with dynamic discovery using `INTELFPGA_DIR`; add `INTELFPGA_DIR` to the documented environment variables |
| `dpi-riscv/install_modelsim.sh` | Change `DEFAULT_INSTALL_DIR` to use `$HOME/intelFPGA/20.1/modelsim_ase` |
| `dpi-riscv/sim/modelsim/implementation_plan_modelsim.md` | Update docs to reflect new variable |

### Implementation Plan (this document)

This file will be updated in place to reflect the new variable.

## Functions

### `dpi-riscv/tests/run_modelsim.sh`

**Modified logic:**
1. Add documented environment variable at top of file:
   ```
   #   INTELFPGA_DIR  — Path to Intel FPGA installation root (default: $HOME/intelFPGA)
   ```
2. Replace the current `MODELSIM_CANDIDATES` static array and the `MODELSIM_DIR`-first
   detection logic with a new function `find_modelsim_from_intelfpga_dir()`:
   - Reads `INTELFPGA_DIR` (default: `$HOME/intelFPGA`)
   - Lists version subdirectories (e.g., `20.1`, `19.1`) sorted by name, takes latest
   - Checks for `$INTELFPGA_DIR/$VERSION/modelsim_ase/bin/vsim` (executable)
3. Keep the existing `MODELSIM_DIR` override as-is (it takes precedence if set) for backwards compatibility
4. Keep the `command -v vsim` fallback (in-PATH detection) as the final fallback

**New logic flow:**
```
if MODELSIM_DIR is set → use it (existing behavior, checked first)
else if INTELFPGA_DIR or default $HOME/intelFPGA exists → auto-discover version
else → try hardcoded paths (existing candidates as fallback)
else → try vsim in PATH (existing behavior)
```

### `dpi-riscv/install_modelsim.sh`

**Modified:**
- Change `DEFAULT_INSTALL_DIR` from `/opt/intelFPGA/20.1/modelsim_ase` to
  `"$HOME/intelFPGA/20.1/modelsim_ase"`
- Update the usage comment and post-install PATH instructions to reflect the new default

## Classes

No class modifications.

## Dependencies

No new dependencies.

## Testing

Manual verification:
1. Set `INTELFPGA_DIR=$HOME/intelFPGA` and run `run_modelsim.sh` — should detect ModelSim
2. Unset `INTELFPGA_DIR` (rely on default `$HOME/intelFPGA`) — should still detect ModelSim
3. Set `INTELFPGA_DIR=/nonexistent` — should fall through gracefully (skip all tests)
4. Run `install_modelsim.sh` — should show new default install path

## Implementation Order

1. Update `implementation_plan_modelsim.md` (this file) to document the plan
2. Modify `install_modelsim.sh` — change `DEFAULT_INSTALL_DIR`
3. Modify `run_modelsim.sh` — add `INTELFPGA_DIR` variable with dynamic discovery logic
4. Quick manual validation
